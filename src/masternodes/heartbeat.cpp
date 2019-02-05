// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "heartbeat.h"
#include "net.h"
#include "util.h"
#include "init.h"
#include "wallet/wallet.h"
#include "snark/libsnark/common/utils.hpp"

namespace
{
std::mutex mutex{};
CHeartBeatTracker* instance{nullptr};
std::array<unsigned char, 16> salt{0x36, 0x4D, 0x2B, 0x44, 0x58, 0x37, 0x78, 0x39, 0x7A, 0x78, 0x5E, 0x58, 0x68, 0x7A, 0x35, 0x75};
}

CHeartBeatMessage::CHeartBeatMessage(const int64_t timestamp)
{
    this->timestamp = timestamp;
}

CHeartBeatMessage::CHeartBeatMessage(CDataStream& stream)
{
    stream >> *this;
}

int64_t CHeartBeatMessage::getTimestamp() const
{
    return timestamp;
}

CHeartBeatMessage::Signature CHeartBeatMessage::getSignature() const
{
    return signature;
}

uint256 CHeartBeatMessage::retrieveHash() const
{
    CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};
    ss << *this;
    return Hash(ss.begin(), ss.end());
}

bool CHeartBeatMessage::isValid() const
{
    return !signature.empty();
}

bool CHeartBeatMessage::signWithKey(const CKey& key)
{
    signature.resize(CPubKey::COMPACT_SIGNATURE_SIZE);
    if (!key.SignCompact(getSignHash(), signature)) {
        signature.clear();
    }
    return isValid();
}

bool CHeartBeatMessage::retrievePubKey(CPubKey& pubKey) const
{
    return isValid() && pubKey.RecoverCompact(getSignHash(), getSignature());
}

uint256 CHeartBeatMessage::getSignHash() const
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    ss << timestamp << salt;
    return Hash(ss.begin(), ss.end());
}

void CHeartBeatTracker::runTickerLoop()
{
    CKeyID operId{};
    CKey operKey{};
//    CMasternodeIDs operId{};
    CHeartBeatTracker tracker{};
    std::int64_t delay{tracker.getMinPeriod(pmasternodesview->allNodes.size()) * 2};

    tracker.startupTime = GetTimeMillis();
    operId.SetHex(GetArg("-masternode-owner", ""));

    while (true) {
        boost::this_thread::interruption_point();
        if (pmasternodesview->nodesByOperator.find(operId) != pmasternodesview->nodesByOperator.end()) {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (!pwalletMain->GetKey(operId, operKey)) {
                throw std::runtime_error("Can't read masternode operator private key");
            }
        }
        if (operKey.IsValid()) {
            tracker.postMessage(operKey);
        }
        MilliSleep(delay);
    }
}

CHeartBeatTracker& CHeartBeatTracker::getInstance()
{
    assert(instance != nullptr);
    return *instance;
}

CHeartBeatMessage CHeartBeatTracker::postMessage(const CKey& signKey, int64_t timestamp)
{
    if (timestamp == 0) {
        timestamp = GetTimeMillis();
    }

    CHeartBeatMessage rv{timestamp};

    if (!rv.signWithKey(signKey)) {
        LogPrintf("%s: Can't sign heartbeat message", __func__);
    } else if (recieveMessage(rv)) {
        broadcastInventory(CInv{MSG_HEARTBEAT, rv.retrieveHash()});
    }

    return std::move(rv);
}

bool CHeartBeatTracker::recieveMessage(const CHeartBeatMessage& message)
{
    bool rv{false};
    CPubKey pubKey{};
    const std::int64_t now{GetTimeMicros()};
    const uint256 hash{message.retrieveHash()};

    if (message.retrievePubKey(pubKey)) {
        const CKeyID operId{pubKey.GetID()};

        if (pmasternodesview->nodesByOperator.find(operId) != pmasternodesview->nodesByOperator.end() &&
            message.getTimestamp() < now + maxHeartbeatInFuture)
        {
            LockGuard lock{mutex};
            libsnark::UNUSED(lock);

            const auto it{keyMessageMap.find(operId)};

            if (it == keyMessageMap.end()) {
                messageList.emplace_front(message);
                keyMessageMap.emplace(operId, messageList.cbegin());
                hashMessageMap.emplace(hash, messageList.cbegin());
                rv = true;
            } else if (message.getTimestamp() - it->second->getTimestamp() >= getMinPeriod(pmasternodesview->allNodes.size())) {
                hashMessageMap.erase(it->second->retrieveHash());
                messageList.erase(it->second);
                messageList.emplace_front(message);
                hashMessageMap.emplace(hash, messageList.cbegin());
                it->second = messageList.cbegin();
                rv = true;
            }
        }
    }
    if (hashMessageMap.find(hash) == hashMessageMap.end()) {
        LogPrintf("%s: Skipping heartbeat (%s,%d) message at %d",
                  __func__,
                  hash.ToString(),
                  message.getTimestamp(),
                  now);
    }

    return rv;
}

bool CHeartBeatTracker::relayMessage(const CHeartBeatMessage& message)
{
    if (recieveMessage(message)) {
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
        const CInv inv{MSG_HEARTBEAT, message.retrieveHash()};

        ss.reserve(1000);
        ss << message;

        mapRelay.insert(std::make_pair(inv, ss));
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));

        broadcastInventory(inv);
        return true;
    }
    return false;
}


std::vector<CHeartBeatMessage> CHeartBeatTracker::getReceivedMessages() const
{
    LockGuard lock{mutex};
    libsnark::UNUSED(lock);
    std::vector<CHeartBeatMessage> rv{};

    assert(messageList.size() == keyMessageMap.size());
    assert(keyMessageMap.size() == hashMessageMap.size());

    rv.reserve(messageList.size());
    rv.assign(messageList.crbegin(), messageList.crend());

    return rv;
}

const CHeartBeatMessage* CHeartBeatTracker::getReceivedMessage(const uint256& hash) const
{
    LockGuard lock{mutex};
    libsnark::UNUSED(lock);

    assert(messageList.size() == keyMessageMap.size());
    assert(keyMessageMap.size() == hashMessageMap.size());

    const auto it{hashMessageMap.find(hash)};
    return it != hashMessageMap.end() ? &*it->second : nullptr;

//    for (const auto & pair : keyMessageMap) {
//        if (pair.second.getHash() == hash) {
//            return &pair.second;
//        }
//    }
//    return nullptr;
}

int CHeartBeatTracker::getMinPeriod(int masternodeCount) const
{
    return std::max(masternodeCount, 30) * 1000;
}

int CHeartBeatTracker::getMaxPeriod(int masternodeCount) const
{
    return getMinPeriod(masternodeCount) * 20;
}

std::vector<CMasternode> CHeartBeatTracker::filterMasternodes(AgeFilter ageFilter) const
{
    std::vector<CMasternode> rv{};
    const auto period{std::make_pair(getMinPeriod(pmasternodesview->allNodes.size()),
                                     getMaxPeriod(pmasternodesview->allNodes.size()))};

    for (const auto& mnPair : pmasternodesview->nodesByOperator) {
        //FIXME: skip my masternode
//        if (idMe == mn) {
//            continue;
//        }

        assert(pmasternodesview->allNodes.find(mnPair.second) != pmasternodesview->allNodes.end());

        const auto& mn{pmasternodesview->allNodes[mnPair.second]};
        const auto it{keyMessageMap.find(mnPair.first)};
        const std::int64_t elapsed{GetTimeMicros() -
                                   std::max(it != keyMessageMap.end() ? it->second->getTimestamp() : startupTime,
                                            chainActive[mn.activationHeight]->GetBlockTime())};

        if ((elapsed < period.first && ageFilter == recentlyFilter) ||
            (elapsed > period.second && ageFilter == outdatedFilter) ||
            (elapsed >= period.first && elapsed < period.second && ageFilter == staleFilter))
        {
            rv.emplace_back(mn);
        }
    }

    return rv;
}

CHeartBeatTracker::CHeartBeatTracker()
{
    assert(instance == nullptr);
    instance = this;
}

CHeartBeatTracker::~CHeartBeatTracker()
{
    assert(instance != nullptr);
    instance = nullptr;
}

void CHeartBeatTracker::broadcastInventory(const CInv& inv) const
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
