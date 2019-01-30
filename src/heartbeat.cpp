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
    return pubKey.RecoverCompact(getSignHash(), getSignature());
}

uint256 CHeartBeatMessage::getSignHash() const
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    ss << timestamp << salt;
    return Hash(ss.begin(), ss.end());
}

void CHeartBeatTracker::runTickerLoop()
{
    CKey operKey{};
    CMasternodeIDs operId{};
    CHeartBeatTracker tracker{};

    tracker.startupTime = GetTimeMillis();

    while (true) {
        boost::this_thread::interruption_point();
        const CMasternodeIDs id{mns::amIOperator()};

        if (id.isNull()) {
            operKey = CKey{};
        } else if (operId.isNull() || operId != id) {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (!pwalletMain->GetKey(id.operatorAuth, operKey)) {
                throw std::runtime_error("Can't read masternode operator private key");
            }
        }
        if (operKey.IsValid()) {
            tracker.postMessage(operKey);
        }
        MilliSleep(tracker.getMinPeriod() * 2 * 1000);
        operId = id;
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
        broadcastInventory(CInv{MSG_HEARTBEAT, rv.getHash()});
    }

    return std::move(rv);
}

bool CHeartBeatTracker::recieveMessage(const CHeartBeatMessage& message)
{
    bool rv{false};
    CPubKey pubKey{};

    if (message.retrievePubKey(pubKey)) {
        const CMasternodeIDs id{mns::findMasternode(uint256{}, pubKey.GetID(), CKeyID{})};

        if (!id.isNull()) {
            LockGuard lock{mutex};
            libsnark::UNUSED(lock);

            const auto it{keyMessageMap.find(pubKey)};

            if (it == keyMessageMap.end()) {
                keyMessageMap.emplace(pubKey, message);
                rv = true;
            } else if (message.getTimestamp() - it->second.getTimestamp() >= getMinPeriod()) {
                it->second = message;
                rv = true;
            }
        }
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
        const CInv inv{MSG_HEARTBEAT, message.getHash()};

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
    std::vector<CHeartBeatMessage> rv{};

    rv.reserve(keyMessageMap.size());
    for (const auto & pair : keyMessageMap) {
        rv.emplace_back(pair.second);
    }

    return rv;
}

bool CHeartBeatTracker::checkMessageWasReceived(const uint256& hash) const
{
    return getReceivedMessage(hash) != nullptr;
}

const CHeartBeatMessage* CHeartBeatTracker::getReceivedMessage(const uint256& hash) const
{
    for (const auto & pair : keyMessageMap) {
        if (pair.second.getHash() == hash) {
            return &pair.second;
        }
    }
    return nullptr;
}

int CHeartBeatTracker::getMinPeriod() const
{
    return std::max(mns::getMasternodeCount(), 30) * 1000;
}

int CHeartBeatTracker::getMaxPeriod() const
{
    return getMinPeriod() * 20 * 1000;
}

std::vector<CMasternode> CHeartBeatTracker::filterMasternodes(AgeFilter ageFilter) const
{
    std::vector<CMasternode> rv{};

//    for (const auto& pair : keyMessageMap) {
////        CMasternode* mn = CMasternode::findMasternode(pubKey.GetID());
//    }

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

//void CHeartBeatTracker::removeObsoleteMessages()
//{
//    std::map<CPubKey, const CHeartBeatMessage*> existent{};

//    for (const auto& pair : recievedMessages) {
//        CPubKey pubKey{};
//        if (!pair.second.retrievePubKey(pubKey)) {
//            recievedMessages.erase(pair.first);
//        } else {
//            auto it{existent.find(pubKey)};
//            if (it == existent.end()) {
//                existent.emplace(pubKey, &pair.second);
//            } else if (pair.second.getTimestamp() < it->second->getTimestamp()) {
//                recievedMessages.erase(pair.first);
//            } else {
//                recievedMessages.erase(it->second->getHash());
//                it->second = &pair.second;
//            }
//        }
//    }
//    assert(existent.size() == recievedMessages.size());

//    for (const auto& pair : existent) {
//        const CMasternodeIDs id{mns::findMasternode(uint256{}, pair.first.GetID(), CKeyID{})};
//        if (id.isNull()) {
//            recievedMessages.erase(pair.second->getHash());
//        }
//    }
//}

