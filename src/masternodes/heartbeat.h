// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERNODES_HEARTBEAT_H
#define MASTERNODES_HEARTBEAT_H

#include "masternodes.h"
#include "../serialize.h"
#include <mutex>
#include <list>

class CKey;
class CInv;

using time_ms = std::int64_t;

class CHeartBeatMessage
{
    using Signature = std::vector<unsigned char>;

public:
    static const int32_t CURRENT_VERSION = 1;
    int32_t nVersion = CURRENT_VERSION;
    
    explicit CHeartBeatMessage(time_ms timestamp = 0);

    time_ms GetTimestamp() const;
    Signature GetSignature() const;
    uint256 GetHash() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
         READWRITE(nVersion);
         READWRITE(timestamp);
         READWRITE(signature);
    }

    void SetNull();
    bool IsNull() const;
    bool SignWithKey(const CKey& key);
    bool GetPubKey(CPubKey& pubKey) const;

private:
    uint256 getSignHash() const;

private:
    time_ms timestamp;
    Signature signature;
};

class CHeartBeatTracker
{
    using LockGuard = std::lock_guard<std::mutex>;
    using MessageList = std::list<CHeartBeatMessage>;
    static constexpr time_ms sec{1000ll};
    static constexpr time_ms maxHeartbeatInFuture{2 * 60 * 60 * sec};


public:

    enum AgeFilter {RECENTLY, STALE, OUTDATED};

    static void runTickerLoop();
    static CHeartBeatTracker& getInstance();

    CHeartBeatMessage postMessage(const CKey& signKey, time_ms timestamp = 0);
    bool recieveMessage(const CHeartBeatMessage& message);
    bool relayMessage(const CHeartBeatMessage& message);

    bool findReceivedMessage(const uint256& hash, CHeartBeatMessage* message = nullptr) const;
    std::vector<CHeartBeatMessage> getReceivedMessages() const;

    time_ms getMinPeriod() const; // minimum allowed time between heartbeats
    time_ms getAvgPeriod() const; // default time between heartbeats
    time_ms getMaxPeriod() const; // maximum allowed time between heartbeats

    CMasternodes filterMasternodes(AgeFilter ageFilter) const;

private:
    CHeartBeatTracker();
    ~CHeartBeatTracker();

private:
    time_ms startupTime;
    MessageList messageList;
    std::map<CKeyID, MessageList::const_iterator> keyMessageMap;
    std::map<uint256, MessageList::const_iterator> hashMessageMap;
};

#endif // MASTERNODES_HEARTBEAT_H
