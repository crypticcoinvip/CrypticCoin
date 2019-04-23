// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "heartbeat.h"
#include "masternodes.h"
#include "../net.h"
#include "../util.h"
#include "../init.h"
#include "../wallet/wallet.h"
#include "../snark/libsnark/common/utils.hpp"
#include <boost/thread.hpp>
#include <main.h>
#include <consensus/validation.h>

namespace
{
CCriticalSection cs{};
CHeartBeatTracker* instance{nullptr};
std::array<unsigned char, 16> salt_{0x36, 0x4D, 0x2B, 0x44, 0x58, 0x37, 0x78, 0x39, 0x7A, 0x78, 0x5E, 0x58, 0x68, 0x7A, 0x35, 0x75};

CKey getMasternodeKey()
{
    CKey rv{};
#ifdef ENABLE_WALLET
    AssertLockHeld(cs_main);
    AssertLockHeld(pwalletMain->cs_wallet);
    const boost::optional<CMasternodesView::CMasternodeIDs> mnId{pmasternodesview->AmIOperator()};
    if (mnId != boost::none) {
        if (!pwalletMain->GetKey(mnId.get().operatorAuthAddress, rv)) {
            LogPrintf("%s: Can't read masternode operator private key", __func__);
            rv = CKey{};
        }
    }
#endif
    return rv;
}

bool checkMasternodeKeyAndStatus(const CKeyID& keyId)
{
    AssertLockHeld(cs_main);
    const auto pair = pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOperator, keyId);
    return pair != boost::none && pmasternodesview->GetMasternodes().at((*pair)->second).deadSinceHeight == -1;
}

}

CHeartBeatMessage::CHeartBeatMessage(time_ms timestamp)
{
    this->timestamp = timestamp;
}

time_ms CHeartBeatMessage::GetTimestamp() const
{
    return timestamp;
}

CHeartBeatMessage::Signature CHeartBeatMessage::GetSignature() const
{
    return signature;
}

uint256 CHeartBeatMessage::GetHash() const
{
    CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};
    ss << *this;
    return Hash(ss.begin(), ss.end());
}

void CHeartBeatMessage::SetNull()
{
    nVersion = CURRENT_VERSION;
    timestamp = 0;
    signature.clear();
}

bool CHeartBeatMessage::IsNull() const
{
    return timestamp == 0 || signature.empty();
}

bool CHeartBeatMessage::SignWithKey(const CKey& key)
{
    signature.resize(CPubKey::COMPACT_SIGNATURE_SIZE);
    if (!key.SignCompact(getSignHash(), signature)) {
        signature.clear();
    }
    return !IsNull();
}

bool CHeartBeatMessage::GetPubKey(CPubKey& pubKey) const
{
    return !IsNull() && pubKey.RecoverCompact(getSignHash(), GetSignature());
}

uint256 CHeartBeatMessage::getSignHash() const
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    ss << nVersion << timestamp << salt_;
    return Hash(ss.begin(), ss.end());
}

void CHeartBeatTracker::runTickerLoop()
{
    CHeartBeatTracker& tracker{CHeartBeatTracker::getInstance()};
    time_ms lastTime{GetTimeMillis()};
    tracker.startupTime = lastTime;

    while (true) {
        boost::this_thread::interruption_point();
        const time_ms currentTime{GetTimeMillis()};

        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (currentTime - lastTime > tracker.getAvgPeriod()) {
                const CKey masternodeKey{getMasternodeKey()};
                if (masternodeKey.IsValid()) {
                    tracker.postMessage(masternodeKey);
                }
                lastTime = currentTime;
            }
        }

        MilliSleep(500);
    }
}

CHeartBeatTracker& CHeartBeatTracker::getInstance()
{
    if (instance == nullptr) {
        LOCK(cs);
        if (instance == nullptr) {
            instance = new CHeartBeatTracker{};
        }
    }

    assert(instance != nullptr);
    return *instance;
}

CHeartBeatMessage CHeartBeatTracker::postMessage(const CKey& signKey, time_ms timestamp)
{
    if (timestamp == 0) {
        timestamp = GetTimeMillis();
    }

    CHeartBeatMessage rv{timestamp};

    CValidationState state;
    if (!rv.SignWithKey(signKey)) {
        LogPrintf("%s: Can't sign heartbeat message", __func__);
    } else if (recieveMessage(rv, state)) {
        BroadcastInventory(CInv{MSG_HEARTBEAT, rv.GetHash()});
    }

    return rv;
}

bool CHeartBeatTracker::recieveMessage(const CHeartBeatMessage& message, CValidationState& state)
{
    bool rv{false};
    CPubKey pubKey{};
    const time_ms now{GetTimeMillis()};
    const uint256 hash{message.GetHash()};

    bool authenticated = false;

    if (message.GetPubKey(pubKey)) {
        AssertLockHeld(cs_main);
        const CKeyID masternodeKey{pubKey.GetID()};

        if (checkMasternodeKeyAndStatus(masternodeKey) &&
            message.GetTimestamp() < now + maxHeartbeatInFuture)
        {
            LOCK(cs);
            authenticated = true;

            const auto it{keyMessageMap.find(masternodeKey)};

            if (it == keyMessageMap.end()) {
                messageList.emplace_front(message);
                keyMessageMap.emplace(masternodeKey, messageList.cbegin());
                hashMessageMap.emplace(hash, messageList.cbegin());
                rv = true;
            } else if (message.GetTimestamp() - it->second->GetTimestamp() >= getMinPeriod()) {
                hashMessageMap.erase(it->second->GetHash());
                messageList.erase(it->second);
                messageList.emplace_front(message);
                hashMessageMap.emplace(hash, messageList.cbegin());
                it->second = messageList.cbegin();
                rv = true;
            }
        }
    }
    if (!authenticated) {
        return state.DoS(IsInitialBlockDownload() ? 0 : 1,
                         error("CHeartBeatTracker(): received not authenticated heartbeat"),
                         REJECT_INVALID, "heartbeat-auth");
    }

    return rv;
}

bool CHeartBeatTracker::relayMessage(const CHeartBeatMessage& message, CValidationState& state)
{
    AssertLockHeld(cs_main);
    if (recieveMessage(message, state)) {
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
        const CInv inv{MSG_HEARTBEAT, message.GetHash()};

        ss.reserve(1000);
        ss << message;

        mapRelay.insert(std::make_pair(inv, ss));
        vRelayExpiration.emplace_back(GetTime() + 15 * 60, inv);

        BroadcastInventory(inv);
        return true;
    }

    return false;
}

bool CHeartBeatTracker::findReceivedMessage(const uint256& hash, CHeartBeatMessage* message) const
{
    LOCK(cs);

    assert(messageList.size() == keyMessageMap.size());
    assert(keyMessageMap.size() == hashMessageMap.size());

    const auto it{hashMessageMap.find(hash)};
    const auto rv{it != this->hashMessageMap.end()};

    if (rv && message != nullptr) {
        *message = *it->second;
    }

    return rv;

//    for (const auto & pair : keyMessageMap) {
//        if (pair.second.getHash() == hash) {
//            return &pair.second;
//        }
//    }
//    return nullptr;
}

std::vector<CHeartBeatMessage> CHeartBeatTracker::getReceivedMessages() const
{
    LOCK(cs);
    std::vector<CHeartBeatMessage> rv{};

    assert(messageList.size() == keyMessageMap.size());
    assert(keyMessageMap.size() == hashMessageMap.size());

    rv.reserve(messageList.size());
    rv.assign(messageList.crbegin(), messageList.crend());

    return rv;
}

time_ms CHeartBeatTracker::getMinPeriod() const
{
    AssertLockHeld(cs_main);
    const time_ms period{Params().GetConsensus().nMasternodesHeartbeatPeriod};
    return std::max((time_ms) pmasternodesview->GetMasternodes().size(), period) * sec;
}

time_ms CHeartBeatTracker::getAvgPeriod() const
{
    return getMinPeriod() * 2;
}

time_ms CHeartBeatTracker::getMaxPeriod() const
{
    if (Params().NetworkIDString() == "regtest")
    {
        return getMinPeriod() * 6;
    }
    return std::max(getMinPeriod() * 20, 12 * 60 * 60 * sec); // 20 minimum periods or 12h, whichever is greater
}

CMasternodes CHeartBeatTracker::filterMasternodes(AgeFilter ageFilter) const
{
    AssertLockHeld(cs_main);
    CMasternodes rv{};
    const auto bounds{std::make_pair(getAvgPeriod() * 2, getMaxPeriod())};

    for (const auto& mnPair : pmasternodesview->GetMasternodesByOperator()) {
        const CMasternode& mn{pmasternodesview->GetMasternodes().at(mnPair.second)};
        if (mn.deadSinceHeight != -1) {
            // skip if masternode is already dead
            continue;
        }
        assert(chainActive[mn.height] != nullptr);

        const auto it{keyMessageMap.find(mnPair.first)};
        const time_ms previousTime{(it != keyMessageMap.end() ? it->second->GetTimestamp() : startupTime)};
        const time_ms previousMaxTime{std::max(previousTime, chainActive[mn.height]->GetBlockTime() * sec)};
        const time_ms elapsed{GetTimeMillis() - previousMaxTime};

        const bool recently = elapsed < bounds.first && ageFilter == RECENTLY;
        const bool stale = elapsed >= bounds.first && elapsed < bounds.second && ageFilter == STALE;
        const bool outdated = elapsed > bounds.second && ageFilter == OUTDATED;
        if (recently || outdated || stale)
        {
            rv.emplace(std::make_pair(mnPair.second, mn));
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
