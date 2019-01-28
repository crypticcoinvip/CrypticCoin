// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "heartbeat.h"
#include "net.h"
#include "util.h"
#include "snark/libsnark/common/utils.hpp"

std::mutex CHeartBeat::mutex{};
CHeartBeat* CHeartBeat::instance{nullptr};

CHeartBeat::CHeartBeat()
{

}
CHeartBeat::~CHeartBeat()
{
}

CHeartBeat& CHeartBeat::getInstance()
{
    if (instance == nullptr) {
        LockGuard lock{mutex};
        libsnark::UNUSED(lock);
        if (instance == nullptr) {
            instance = new CHeartBeat{};
        }
    }
    assert(instance != nullptr);
    return *instance;
}

void CHeartBeat::shutdown()
{
    delete instance;
    instance = nullptr;
}

void CHeartBeat::postToAll(int timestamp)
{
    if (timestamp == 0) {
        timestamp = static_cast<int>(GetTimeMillis() / 1000);
    }

    uint256 hash{};
    {
        CDataStream ss{SER_GETHASH, 0};
        ss << timestamp;
        hash = Hash(ss.begin(), ss.end());
    }

    std::vector <CInv> inventories{};
    std::vector<CNode*> nodesCopy{};
    CInv inv{MSG_HEARTBEAT, hash};

    inventories.push_back(inv);
    recieveMessage(hash, timestamp);
    {
        LOCK(cs_vNodes);
        nodesCopy = vNodes;
    }
    for (auto&& node : nodesCopy) {
        if (node == nullptr || node->fDisconnect || node->nVersion < PROTOCOL_VERSION) {
            continue;
        }
        node->PushMessage("inv", inventories);
    }
}

void CHeartBeat::relayMessage(const uint256 &hash, int timestamp)
{
    CInv inv{MSG_HEARTBEAT, hash};
    CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};

    ss.reserve(10000);
    ss << hash << timestamp;

    recieveMessage(hash, timestamp);

    {
        LOCK(cs_mapRelay);
        // Expire old relay messages
        while (!vRelayExpiration.empty() && vRelayExpiration.front().first < GetTime())
        {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        // Save original serialized message so newer versions are preserved
        mapRelay.insert(std::make_pair(inv, ss));
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
    }

    LOCK(cs_vNodes);
    for (auto&& node : vNodes) {
        node->PushInventory(inv);
    }
}

void CHeartBeat::recieveMessage(const uint256& hash, int timestamp)
{
    LockGuard lock{mutex};
    libsnark::UNUSED(lock);

    messages.insert(std::make_pair(hash, timestamp));
}

void CHeartBeat::forgetMessage(const uint256& hash)
{
    LockGuard lock{mutex};
    libsnark::UNUSED(lock);

    messages.erase(hash);
}

bool CHeartBeat::checkMessageIsRecieved(const uint256& hash)
{
    LockGuard lock{mutex};
    libsnark::UNUSED(lock);

    return messages.find(hash) != messages.end();
}

int CHeartBeat::getMessageTimestamp(const uint256 &hash) const
{
    LockGuard lock{mutex};
    libsnark::UNUSED(lock);

    const auto it = messages.find(hash);
    return it == messages.end() ? 0 : it->second;
}

int CHeartBeat::getLastMessageTimestamp() const
{
    using Pair = decltype(messages)::value_type;

    LockGuard lock{mutex};
    libsnark::UNUSED(lock);

    if (messages.empty()) {
        return 0;
    }
    return std::max_element(messages.begin(), messages.end(), [](const Pair & p1, const Pair & p2) {
        return p1.second < p2.second;
    })->second;
}

