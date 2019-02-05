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

void broadcastInventory(const CInv& inv)
{
    LOCK(cs_vNodes);
    for (auto&& node : vNodes) {
        if (node != nullptr &&
            !node->fDisconnect &&
            node->nVersion >= PROTOCOL_VERSION)
        {
            node->PushInventory(inv);
        }
    }
}

bool buildProgenitorVote(const CKey& key, const uint256& blockHash, dpos::ProgenitorVote& vote)
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};

    vote.roundNumber = 1;
    vote.headerBlockHash = blockHash;
    vote.headerSignature.resize(CPubKey::COMPACT_SIGNATURE_SIZE);

    ss << vote.roundNumber << vote.headerBlockHash << salt1;

    if (key.SignCompact(Hash(ss.begin(), ss.end()), vote.headerSignature)) {
        ss.clear();
        vote.tipBlockHash = *chainActive.Tip()->phashBlock;
        vote.progenitorBlockHash = blockHash;
        vote.bodySignature.resize(CPubKey::COMPACT_SIGNATURE_SIZE);

        ss << vote.roundNumber
           << vote.headerBlockHash
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
    headerBlockHash.SetNull();
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
    return IsActivationHeight(chainActive.Tip()->nHeight, params.GetConsensus(), Consensus::UPGRADE_SAPLING) &&
           pmasternodesview->activeNodes.size() > params.GetMinimalMasternodeCount();
}

void dpos::postProgenitorBlock(const CBlock& block)
{
    CInv inv{MSG_PROGENITOR_BLOCK, block.GetHash()};
    recieveProgenitorBlock(block);
    broadcastInventory(inv);
}

void dpos::relayProgenitorBlock(const CBlock& block)
{
    recieveProgenitorBlock(block);
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
    broadcastInventory(inv);
}

void dpos::recieveProgenitorBlock(const CBlock& block)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);

    CKey operKey{};
    const uint256 blockHash{block.GetHash()};

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
    }
    if (operKey.IsValid()) {
        ProgenitorVote vote{};
        if (buildProgenitorVote(operKey, blockHash, vote)) {
            postProgenitorVote(vote);
        } else {
            LogPrintf("%s: Can't build progenitor vote for pre-block %s", __func__, blockHash.ToString());
        }
    }
}

const CBlock* dpos::getReceivedProgenitorBlock(const uint256& hash)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    const auto it{recievedProgenitorBlocks_.find(hash)};
    return it == recievedProgenitorBlocks_.end() ? nullptr : &it->second;
}

void dpos::postProgenitorVote(const ProgenitorVote& vote)
{
    CInv inv{MSG_PROGENITOR_VOTE, vote.GetHash()};
    recieveProgenitorVote(vote);
    broadcastInventory(inv);
}

void dpos::relayProgenitorVote(const ProgenitorVote& vote)
{
    recieveProgenitorVote(vote);
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
    broadcastInventory(inv);
}

void dpos::recieveProgenitorVote(const ProgenitorVote& vote)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    recievedProgenitorVotes_.emplace(vote.GetHash(), vote);
}

const dpos::ProgenitorVote* dpos::getReceivedProgenitorVote(const uint256& hash)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    const auto it{recievedProgenitorVotes_.find(hash)};
    return it == recievedProgenitorVotes_.end() ? nullptr : &it->second;
}
