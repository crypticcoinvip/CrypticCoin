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
#include "../validationinterface.h"
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
std::array<unsigned char, 16> salt{0x4D, 0x48, 0x7A, 0x52, 0x5D, 0x4D, 0x37, 0x78, 0x42, 0x36, 0x5B, 0x64, 0x44, 0x79, 0x59, 0x4F};

class ValidationListener : public CValidationInterface
{

protected:
    void UpdatedBlockTip(const CBlockIndex* pindex) override
    {
//        const auto removeProgenitorVote{[pindex](const std::pair<uint256, dpos::ProgenitorVote>& pair) {
//            return pindex->pprev != nullptr &&
//                   pair.second.tipBlockHash == pindex->pprev->GetBlockHash();
//        }};
//        const auto removeProgenitorBlock{[pindex](const std::pair<uint256, CBlock>& pair) {
//            return pindex->pprev != nullptr &&
//                   pair.second.hashPrevBlock == pindex->pprev->GetBlockHash();
//        }};
        libsnark::UNUSED(pindex);
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);
        recievedProgenitorVotes_.clear();
        recievedProgenitorBlocks_.clear();
//        recievedProgenitorVotes_.erase( std::remove_if(recievedProgenitorVotes_.begin(), recievedProgenitorVotes_.end(), removeProgenitorVote),
//                                        recievedProgenitorVotes_.end());
//        recievedProgenitorBlocks_.erase( std::remove_if(recievedProgenitorBlocks_.begin(), recievedProgenitorBlocks_.end(), removeProgenitorBlock),
//                                         recievedProgenitorBlocks_.end());
        //TODO: clear recieved items on block disconnected
    }
} validationListener_;

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

CKey extractOperatorKey()
{
    CKey rv{};
#ifdef ENABLE_WALLET
    const boost::optional<mns::CMasternodeIDs> mnId{mns::amIActiveOperator()};
    if (mnId) {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        if (!pwalletMain->GetKey(mnId.get().operatorAuthAddress, rv)) {
            rv = CKey{};
        }
    }
#endif
    return rv;
}

const CBlock* findProgenitorBlock(const uint256& dposBlcokHash)
{
    for (const auto& pair: recievedProgenitorVotes_) {
        if (pair.second.dposBlockHash == dposBlcokHash &&
            recievedProgenitorBlocks_.find(pair.second.progenitorBlockHash) != recievedProgenitorBlocks_.end())
        {
            return &recievedProgenitorBlocks_[pair.second.progenitorBlockHash];
        }
    }
    return nullptr;
}

const dpos::ProgenitorVote* findMyVote(const CKey& key)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);

    for (const auto& pair: recievedProgenitorVotes_) {
        CPubKey pubKey{};
        CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
        ss << pair.second.roundNumber
           << pair.second.dposBlockHash
           << pair.second.tipBlockHash
           << pair.second.progenitorBlockHash
           << salt;
        if (pubKey.RecoverCompact(Hash(ss.begin(), ss.end()), pair.second.authSignature)) {
            if (pubKey == key.GetPubKey()) {
                return &pair.second;
            }
        }

    }
    return nullptr;
}

CBlock tranformProgenitorBlock(const CBlock& progenitorBlock)
{
    CBlock rv{progenitorBlock.GetBlockHeader()};
    rv.roundNumber = progenitorBlock.roundNumber;
    rv.vtx.resize(progenitorBlock.vtx.size());
    std::copy(progenitorBlock.vtx.begin(), progenitorBlock.vtx.end(), rv.vtx.begin());
    rv.hashMerkleRoot = rv.BuildMerkleTree();
    //        attachTransactions(dposBlock);
    return rv;
}

bool voteForProgenitorBlock(const CBlock& progenitorBlock, const CKey& operatorKey)
{
    if (operatorKey.IsValid() && findMyVote(operatorKey) == nullptr) {
        dpos::ProgenitorVote vote{};
        CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
        CBlock dposBlock{tranformProgenitorBlock(progenitorBlock)};

        vote.roundNumber = progenitorBlock.roundNumber;
        vote.dposBlockHash = dposBlock.GetHash();
        vote.tipBlockHash = progenitorBlock.hashPrevBlock;
        vote.progenitorBlockHash = progenitorBlock.GetHash();
        vote.authSignature.resize(CPubKey::COMPACT_SIGNATURE_SIZE);

        ss << vote.roundNumber
           << vote.dposBlockHash
           << vote.tipBlockHash
           << vote.progenitorBlockHash
           << salt;

        if (operatorKey.SignCompact(Hash(ss.begin(), ss.end()), vote.authSignature)) {
            dpos::postProgenitorVote(vote);
        } else {
            LogPrintf("%s: Can't vote for pre-block %s", __func__, progenitorBlock.GetHash().GetHex());
        }
    }
}

bool checkProgenitorBlockIsConvenient(const CBlock& block)
{
    LOCK(cs_main);
    return block.hashPrevBlock == chainActive.Tip()->GetBlockHash();
}

bool checkProgenitorVoteIsConvenient(const dpos::ProgenitorVote& vote)
{
    return vote.tipBlockHash == chainActive.Tip()->GetBlockHash();
}

void printBlock(const CBlock& block)
{
    const auto toHex{[](const std::vector<unsigned char>& bin) {
         std::ostringstream ss{};
         for (const auto& v: bin) {
            ss << std::hex << static_cast<int>(v) << ':';
         }
         return ss.str();
    }};
    LogPrintf("%s: hash: %s, hashPrev: %s, merkleRoot: %s, merkleRoot_PoW: %s, round: %d, bits: %d, time: %d, solution: %s\n",
              __func__,
              block.GetHash().GetHex(),
              block.hashPrevBlock.GetHex(),
              block.hashMerkleRoot.GetHex(),
              block.hashMerkleRoot_PoW.GetHex(),
              block.roundNumber,
              block.nBits,
              block.nTime,
              toHex(block.nSolution));
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
    tipBlockHash.SetNull();
    progenitorBlockHash.SetNull();
    authSignature.clear();
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
    if (recieveProgenitorBlock(block, true)) {
        BroadcastInventory({MSG_PROGENITOR_BLOCK, block.GetHash()});
    }
}

void dpos::relayProgenitorBlock(const CBlock& block)
{
    if (recieveProgenitorBlock(block, false)) {
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

bool dpos::recieveProgenitorBlock(const CBlock& block, bool isMe)
{
    bool rv{false};
    libsnark::UNUSED(isMe);

    if (checkProgenitorBlockIsConvenient(block)) {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);
        rv = recievedProgenitorBlocks_.emplace(block.GetHash(), block).second;
    }

    if (rv) {
        voteForProgenitorBlock(block, extractOperatorKey());
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
    if (recieveProgenitorVote(vote, true)) {
        LogPrintf("%s: Post my vote %s for pre-block %s on round %d\n",
                  __func__,
                  vote.GetHash().GetHex(),
                  vote.progenitorBlockHash.GetHex(),
                  vote.roundNumber);
        BroadcastInventory(CInv{MSG_PROGENITOR_VOTE, vote.GetHash()});
    }
}

void dpos::relayProgenitorVote(const ProgenitorVote& vote)
{
    if (recieveProgenitorVote(vote, false)) {
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

bool dpos::recieveProgenitorVote(const ProgenitorVote& vote, bool isMe)
{
    using PVPair = std::pair<uint256, std::size_t>;
    std::map<PVPair::first_type, PVPair::second_type> progenitorVotes{};

    if (checkProgenitorVoteIsConvenient(vote)) {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);

        LogPrintf("%s: Pre-block vote recieved: %d\n", __func__, recievedProgenitorVotes_.count(vote.GetHash()));
        if (recievedProgenitorVotes_.emplace(vote.GetHash(), vote).second) {
            for (const auto& pair : recievedProgenitorVotes_) {
                progenitorVotes[pair.second.dposBlockHash]++;
            }
        }
    }

    const auto itEnd{progenitorVotes.end()};
    const auto itMax{std::max_element(progenitorVotes.begin(), itEnd, [](const PVPair& p1, const PVPair& p2) {
        return p1.second < p2.second;
    })};
    if (itMax == itEnd) {
        LogPrintf("%s: Ignoring duplicating pre-block vote: %s\n", __func__, vote.GetHash().GetHex());
        return false;
    }

    const CKey operKey{extractOperatorKey()};
    if (operKey.IsValid()) {
        LogPrintf("%s: Pre-block vote rate: %d\n", __func__, 1.0 * itMax->second / pmasternodesview->activeNodes.size());

        if (isMe && 1.0 * itMax->second / pmasternodesview->activeNodes.size() >= 2.0 / 3.0) {
            const CBlock* pBlock{findProgenitorBlock(itMax->first)};
            if (pBlock != nullptr) {
                CValidationState state{};
                CBlock dposBlock{tranformProgenitorBlock(*pBlock)};

                if (dposBlock.GetHash() != itMax->first ||
                    !ProcessNewBlock(state, NULL, &dposBlock, true, NULL))
                {
                    LogPrintf("%s: Can't create new dpos block\n");
                }
            }
        }
//        else if (recievedProgenitorVotes_.size() == pmasternodesview->activeNodes.size()) {
//            int roundNumber{recievedProgenitorVotes_.begin()->second.roundNumber + 1};
//            recievedProgenitorVotes_.clear();
//            assert(!recievedProgenitorBlocks_.empty());
//            voteForProgenitorBlock(recievedProgenitorBlocks_.begin()->second, operKey, roundNumber);
//        }
    }
    return true;
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

CValidationInterface* dpos::getValidationListener()
{
    return &validationListener_;
}
