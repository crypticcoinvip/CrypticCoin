// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_HEARTBEAT_H
#define BITCOIN_HEARTBEAT_H

#include "uint256.h"
#include "serialize.h"
#include <mutex>

class CKey;
class CInv;
class CMasternode;
class CDataStream;

class CHeartBeatMessage
{
    using Signature = std::vector<unsigned char>;

public:
    explicit CHeartBeatMessage(const std::int64_t timestamp);
    explicit CHeartBeatMessage(CDataStream& stream);
    explicit CHeartBeatMessage(const CHeartBeatMessage&) = default;

    std::int64_t getTimestamp() const;
    Signature getSignature() const;
    uint256 getHash() const;
    uint256 getSignHash() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
         READWRITE(timestamp);
         READWRITE(signature);
    }

    bool signWithKey(const CKey& key);

private:
    std::int64_t timestamp;
    Signature signature;
};

class CHeartBeatTracker
{
    using LockGuard = std::lock_guard<std::mutex>;

public:
    static CHeartBeatTracker& getInstance();

    void postMessage(const CKey& signKey, std::int64_t timestamp = 0);
    void relayMessage(const CHeartBeatMessage& message);
    void recieveMessage(const CHeartBeatMessage& message);

    bool checkMessageWasReceived(const uint256& hash) const;
    const CHeartBeatMessage* getReceivedMessage(const uint256& hash) const;

    std::int64_t getMinPeriod();
    std::int64_t getMaxPeriod();

    std::vector<CHeartBeatMessage> getReceivedMessages() const;

    static void runInBackground();

    std::vector<CMasternode*> getOutdatedMasternodes() const;
    std::int64_t getOutdatedMasternodeTime(const CMasternode* masternode);

private:
    CHeartBeatTracker();
    ~CHeartBeatTracker();


    void broadcastInventory(const CInv& inv);

private:
    time_t startupTime;
    CMasternode* masternode;
    std::map<uint256, CHeartBeatMessage> recievedMessages;
//    std::map<CKeyID, std::int64_t> masternodeHertbeats;
};

#endif
