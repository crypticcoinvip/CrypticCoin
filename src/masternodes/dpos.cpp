// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dpos.h"
#include "util.h"
#include "../init.h"
#include "../protocol.h"
#include "../net.h"
#include "../chainparams.h"
#include "../wallet/wallet.h"
#include "../masternodes/masternodes.h"
#include "../consensus/upgrades.h"
#include "../consensus/validation.h"
#include "../snark/libsnark/common/utils.hpp"
#include <mutex>

namespace
{
using LockGuard = std::lock_guard<std::mutex>;

std::mutex mutex_{};
std::map<uint256, CBlock> recievedProgenitorBlocks_{};
std::map<uint256, dpos::ProgenitorVote> recievedProgenitorVotes_{};
std::array<unsigned char, 16> salt1{0x4D, 0x48, 0x7A, 0x52, 0x5D, 0x4D, 0x37, 0x78, 0x42, 0x36, 0x5B, 0x64, 0x44, 0x79, 0x59, 0x4F};
std::array<unsigned char, 16> salt2{0x35, 0x2D, 0x61, 0x51, 0x48, 0x30, 0x2F, 0x2C, 0x4D, 0x3E, 0x3F, 0x74, 0x3C, 0x29, 0x47, 0x35};

void attachTransactions(CBlock& block)
{
    const int nHeight{chainActive.Tip()->nHeight + 1};
    const int64_t nMedianTimePast{chainActive.Tip()->GetMedianTimePast()};

    block.vtxSize_dPoS = block.vtx.size();
    for (auto mi{mempool.mapTx.begin()}; mi != mempool.mapTx.end(); ++mi) {
        const CTransaction& tx = mi->GetTx();
        const int64_t nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                                      ? nMedianTimePast
                                      : block.GetBlockTime();

        if (!tx.IsCoinBase() && IsFinalTx(tx, nHeight, nLockTimeCutoff) && !IsExpiredTx(tx, nHeight)) {
            block.vtx.push_back(tx);
        }
    }
    block.vtxSize_dPoS = block.vtx.size() - block.vtxSize_dPoS;
}

bool buildProgenitorVote(const CKey& key, const uint256& progenitorBlockHash, const uint256& dposBlockHash, dpos::ProgenitorVote& vote)
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    vote.roundNumber = 1;
    vote.dposBlockHash = dposBlockHash;
    vote.headerSignature.resize(CPubKey::COMPACT_SIGNATURE_SIZE);

    ss << vote.roundNumber << vote.dposBlockHash << salt1;

    if (key.SignCompact(Hash(ss.begin(), ss.end()), vote.headerSignature)) {
        ss.clear();
        vote.tipBlockHash = chainActive.Tip()->GetBlockHash();
        vote.progenitorBlockHash = progenitorBlockHash;
        vote.bodySignature.resize(CPubKey::COMPACT_SIGNATURE_SIZE);

        ss << vote.roundNumber
           << vote.dposBlockHash
           << salt1
           << vote.headerSignature
           << vote.tipBlockHash
           << vote.progenitorBlockHash;

        return key.SignCompact(Hash(ss.begin(), ss.end()), vote.bodySignature);
    }

    return false;
}

}

dpos::ProgenitorVote::ProgenitorVote()
{
    SetNull();
}

bool dpos::ProgenitorVote::IsNull() const
{
    return roundNumber == 0;
}

void dpos::ProgenitorVote::SetNull()
{
    dposBlockHash.SetNull();
    roundNumber = 0;
    headerSignature.clear();
    tipBlockHash.SetNull();
    progenitorBlockHash.SetNull();
    bodySignature.clear();
}

uint256 dpos::ProgenitorVote::GetHash() const
{
    return SerializeHash(*this);
}

bool dpos::checkIsActive()
{
    const CChainParams& params{Params()};
    return NetworkUpgradeActive(chainActive.Height(), params.GetConsensus(), Consensus::UPGRADE_SAPLING) &&
           pmasternodesview->activeNodes.size() >= params.GetMinimalMasternodeCount();
}

void dpos::postProgenitorBlock(const CBlock& block)
{
    if (recieveProgenitorBlock(block)) {
        BroadcastInventory({MSG_PROGENITOR_BLOCK, block.GetHash()});
    }
}

void dpos::relayProgenitorBlock(const CBlock& block)
{
    if (recieveProgenitorBlock(block)) {
        // Expire old relay messages
        LOCK(cs_mapRelay);
        while (!vRelayExpiration.empty() &&
               vRelayExpiration.front().first < GetTime())
        {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        // Save original serialized message so newer versions are preserved
        CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};
        const CInv inv{MSG_PROGENITOR_BLOCK, block.GetHash()};

        ss.reserve(1000);
        ss << block;

        mapRelay.insert(std::make_pair(inv, ss));
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
        BroadcastInventory(inv);
    }
}

bool dpos::recieveProgenitorBlock(const CBlock& block)
{
    bool rv{false};
    CKey operKey{};
    const uint256 blockHash{block.GetHash()};

    if (block.hashPrevBlock == chainActive.Tip()->GetBlockHash()) {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);

        if (recievedProgenitorBlocks_.emplace(blockHash, block).second) {
            const boost::optional<mns::CMasternodeIDs> mnId{mns::amIActiveOperator()};
            if (mnId) {
#ifdef ENABLE_WALLET
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->GetKey(mnId.get().operatorAuthAddress, operKey)) {
                    operKey = CKey{};
                }
#endif
            }
            rv = true;
        }
    }

    if (operKey.IsValid()) {
        ProgenitorVote vote{};
        CBlock dposBlock{block.GetBlockHeader()};

        attachTransactions(dposBlock);
        dposBlock.hashMerkleRoot = dposBlock.BuildMerkleTree();

        if (buildProgenitorVote(operKey, blockHash, dposBlock.GetHash(), vote)) {
            postProgenitorVote(vote);
        } else {
            LogPrintf("%s: Can't build progenitor vote for pre-block %s", __func__, blockHash.ToString());
        }
    }

    return rv;
}

const CBlock* dpos::getReceivedProgenitorBlock(const uint256& hash)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    const auto it{recievedProgenitorBlocks_.find(hash)};
    return it == recievedProgenitorBlocks_.end() ? nullptr : &it->second;
}

std::vector<CBlock> dpos::listReceivedProgenitorBlocks()
{
    std::vector<CBlock> rv{};
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);

    rv.reserve(recievedProgenitorBlocks_.size());

    for (const auto& pair : recievedProgenitorBlocks_) {
        assert(pair.first == pair.second.GetHash());
        rv.emplace_back(pair.second);
    }

    return rv;
}


void dpos::postProgenitorVote(const ProgenitorVote& vote)
{
    if (recieveProgenitorVote(vote)) {
        BroadcastInventory(CInv{MSG_PROGENITOR_VOTE, vote.GetHash()});
    }
}

void dpos::relayProgenitorVote(const ProgenitorVote& vote)
{
    if (recieveProgenitorVote(vote)) {
        // Expire old relay messages
        LOCK(cs_mapRelay);
        while (!vRelayExpiration.empty() &&
               vRelayExpiration.front().first < GetTime())
        {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        // Save original serialized message so newer versions are preserved
        CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};
        const CInv inv{MSG_PROGENITOR_VOTE, vote.GetHash()};

        ss.reserve(1000);
        ss << vote;

        mapRelay.insert(std::make_pair(inv, ss));
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
        BroadcastInventory(inv);
    }
}

bool dpos::recieveProgenitorVote(const ProgenitorVote& vote)
{
    bool rv{false};
    std::map<uint256, std::map<uint256, int>> progenitorVotes{};

    {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);

        if (recievedProgenitorVotes_.emplace(vote.GetHash(), vote).second) {
            for (const auto& pair : recievedProgenitorVotes_) {
                progenitorVotes[pair.second.progenitorBlockHash][pair.second.dposBlockHash]++;
            }
            rv = true;
        }
    }

    for (const auto& pair : progenitorVotes) {
        using PVPair = std::pair<uint256, int>;
        const auto it{std::max_element(pair.second.begin(), pair.second.end(), [](const PVPair& p1, const PVPair& p2) {
            return p1.second < p2.second;
        })};
        assert(it != pair.second.end());

        if (it->second / pmasternodesview->activeNodes.size() > 0.66) {
            assert(recievedProgenitorBlocks_.find(pair.first) != recievedProgenitorBlocks_.end());

            const CBlock& progenitorBlock{recievedProgenitorBlocks_[pair.first]};
            CBlock dposBlock{progenitorBlock.GetBlockHeader()};

            dposBlock.vtx = progenitorBlock.vtx;
            attachTransactions(dposBlock);

            if (dposBlock.GetHash() == it->first) {
                CValidationState state;
                if (!ProcessNewBlock(state, NULL, &dposBlock, true, NULL)) {
                    LogPrintf("%s: Can't process new dpos block");
                }
            }
            break;
        }
    }

    return rv;
}

const dpos::ProgenitorVote* dpos::getReceivedProgenitorVote(const uint256& hash)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    const auto it{recievedProgenitorVotes_.find(hash)};
    return it == recievedProgenitorVotes_.end() ? nullptr : &it->second;
}


std::vector<dpos::ProgenitorVote> dpos::listReceivedProgenitorVotes()
{
    std::vector<ProgenitorVote> rv{};
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);

    rv.reserve(recievedProgenitorVotes_.size());

    for (const auto& pair : recievedProgenitorVotes_) {
        assert(pair.first == pair.second.GetHash());
        rv.emplace_back(pair.second);
    }

    return rv;
}
