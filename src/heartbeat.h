// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_HEARTBEAT_H
#define BITCOIN_HEARTBEAT_H

#include "masternode.h"
#include "serialize.h"
#include <mutex>

class CKey;
class CInv;
class CDataStream;

class CHeartBeatMessage
{
    using Signature = std::vector<unsigned char>;

public:
    explicit CHeartBeatMessage(const std::int64_t timestamp);
    explicit CHeartBeatMessage(CDataStream& stream);

    std::int64_t getTimestamp() const;
    Signature getSignature() const;
    uint256 getHash() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
         READWRITE(timestamp);
         READWRITE(signature);
    }

    bool isValid() const;
    bool signWithKey(const CKey& key);
    bool retrievePubKey(CPubKey& pubKey) const;

private:
    uint256 getSignHash() const;

private:
    std::int64_t timestamp;
    Signature signature;
};

class CHeartBeatTracker
{
    using LockGuard = std::lock_guard<std::mutex>;

public:
    enum AgeFilter { Recently, Stale, Outdated };

    static void runTickerLoop();
    static CHeartBeatTracker& getInstance();

    CHeartBeatMessage postMessage(const CKey& signKey, std::int64_t timestamp = 0);
    bool recieveMessage(const CHeartBeatMessage& message);
    bool relayMessage(const CHeartBeatMessage& message);

    std::vector<CHeartBeatMessage> getReceivedMessages() const;
    bool checkMessageWasReceived(const uint256& hash) const;
    const CHeartBeatMessage* getReceivedMessage(const uint256& hash) const;

    int getMinPeriod() const;
    int getMaxPeriod() const;

    std::vector<CMasternode> filterMasternodes(AgeFilter ageFilter) const;

private:
    CHeartBeatTracker();
    ~CHeartBeatTracker();

    void broadcastInventory(const CInv& inv) const;
//    void removeObsoleteMessages();

private:
    time_t startupTime;
    std::map<CPubKey, CHeartBeatMessage> keyMessageMap;
};

#endif
