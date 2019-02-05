// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dpos.h"
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
std::map<uint256, CBlock> recievedProgenitors_{};

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
}

bool dpos::checkActiveMode()
{
    const CChainParams& params{Params()};
    return IsActivationHeight(chainActive.Tip()->nHeight, params.GetConsensus(), Consensus::UPGRADE_SAPLING) &&
           pmasternodesview->activeNodes.size() > params.GetMinimalMasternodeCount();
}

void dpos::postBlockProgenitor(const CBlock& pblock)
{
    CInv inv{MSG_PROGENITOR, pblock.GetHash()};
    recieveBlockProgenitor(pblock);
    broadcastInventory(inv);
}

void dpos::relayBlockProgenitor(const CBlock& pblock)
{
    recieveBlockProgenitor(pblock);
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
    const CInv inv{MSG_BLOCK, pblock.GetHash()};

    ss.reserve(1000);
    ss << pblock;

    mapRelay.insert(std::make_pair(inv, ss));
    vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
    broadcastInventory(inv);
}

void dpos::recieveBlockProgenitor(const CBlock& pblock)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    recievedProgenitors_.emplace(pblock.GetHash(), pblock);
}

const CBlock* dpos::getReceivedBlockProgenitor(const uint256& hash)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    const auto it{recievedProgenitors_.find(hash)};
    return it == recievedProgenitors_.end() ? nullptr : &it->second;
}
