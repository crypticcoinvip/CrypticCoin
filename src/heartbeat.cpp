// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "heartbeat.h"
#include "masternode.h"
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

uint256 CHeartBeatMessage::getHash() const
{
    CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};
    ss << *this;
    return Hash(ss.begin(), ss.end());
}

uint256 CHeartBeatMessage::getSignHash() const
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    ss << timestamp << salt;
    return Hash(ss.begin(), ss.end());
}

bool CHeartBeatMessage::signWithKey(const CKey& key)
{
    return key.SignCompact(getSignHash(), signature);
}

CHeartBeatTracker::CHeartBeatTracker()
{
    assert(instance == nullptr);
    instance = this;
    masternode = nullptr;
}

CHeartBeatTracker::~CHeartBeatTracker()
{
    assert(instance != nullptr);
    instance = nullptr;
}

void CHeartBeatTracker::broadcastInventory(const CInv& inv)
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

CHeartBeatTracker& CHeartBeatTracker::getInstance()
{
    assert(instance != nullptr);
    return *instance;
}

void CHeartBeatTracker::postMessage(const CKey& signKey, int64_t timestamp)
{
    if (timestamp == 0) {
        timestamp = GetTimeMillis();
    }

    CHeartBeatMessage message{timestamp};

    if (!message.signWithKey(signKey)) {
        LogPrintf("%s: Can't sign heartbeat message", __func__);
    } else {
        const CInv inv{MSG_HEARTBEAT, message.getHash()};
        recieveMessage(message);
        broadcastInventory(inv);
    }
}

void CHeartBeatTracker::relayMessage(const CHeartBeatMessage& message)
{
    CInv inv{MSG_HEARTBEAT, message.getHash()};
    recieveMessage(message);

    {
        LOCK(cs_mapRelay);
        // Expire old relay messages
        while (!vRelayExpiration.empty() && vRelayExpiration.front().first < GetTime()) {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        // Save original serialized message so newer versions are preserved
        CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};
        ss.reserve(1000);
        ss << message;
        mapRelay.insert(std::make_pair(inv, ss));
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
    }

    broadcastInventory(inv);
}

void CHeartBeatTracker::recieveMessage(const CHeartBeatMessage& message)
{
    LockGuard lock{mutex};
    libsnark::UNUSED(lock);

    recievedMessages.emplace(message.getHash(), message);
}

bool CHeartBeatTracker::checkMessageWasReceived(const uint256& hash) const
{
    return recievedMessages.find(hash) != recievedMessages.end();
}

const CHeartBeatMessage* CHeartBeatTracker::getReceivedMessage(const uint256& hash) const
{
    const auto it = recievedMessages.find(hash);
    return it != recievedMessages.end() ? &it->second : nullptr;
}

void CHeartBeatTracker::runInBackground()
{
    CKey key{};
    CHeartBeatTracker tracker{};
    tracker.masternode = CMasternode::amIMasternode();

    if (tracker.masternode != nullptr) {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        if (!pwalletMain->GetKey(tracker.masternode->getAddress(), key)) {
            throw std::runtime_error("Can't read masternode private key");
        }

        while (instance->masternode != nullptr) {
            boost::this_thread::interruption_point();
            tracker.postMessage(key);
            MilliSleep(tracker.getMinPeriod() * 2 * 1000);
            //!TODO clean old recievedMessages
//            LockGuard lock{mutex};
//            libsnark::UNUSED(lock);
//            for (auto&& pair : recievedMessages) {
//                if (pair.)
//            }
        }
    }
}

std::vector<CMasternode*> CHeartBeatTracker::getOutdatedMasternodes() const
{
    std::vector<CMasternode*> rv{};

    for (auto&& pair : recievedMessages) {
        CPubKey pubKey{};
        pubKey.RecoverCompact(pair.second.getSignHash(), pair.second.getSignature());

        for (auto&& mn : CMasternode::getAvailableList()) {
            if (mn->getAddress() == pubKey.GetID()) {
                rv.push_back(mn);
            }
        }
    }
    return rv;
}

int64_t CHeartBeatTracker::getOutdatedMasternodeTime(const CMasternode* masternode)
{
    for (auto&& mn : getOutdatedMasternodes()) {
        if (mn->getAddress() == masternode->getAddress()) {

        }
    }
}

int64_t CHeartBeatTracker::getMinPeriod()
{
    return 30;
}

int64_t CHeartBeatTracker::getMaxPeriod()
{
    return getMinPeriod() * 20;
}

std::vector<CHeartBeatMessage> CHeartBeatTracker::getReceivedMessages() const
{
    std::vector<CHeartBeatMessage> rv{};
    rv.reserve(recievedMessages.size());
    for (const auto & pair : recievedMessages) {
        rv.emplace_back(pair.second);
    }
    return rv;
}


