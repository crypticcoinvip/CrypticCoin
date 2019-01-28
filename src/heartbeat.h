// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_HEARTBEAT_H
#define BITCOIN_HEARTBEAT_H

#include "uint256.h"
#include <mutex>
#include <map>

class uint256;

class CHeartBeat
{
    using LockGuard = std::lock_guard<std::mutex>;

public:
    static CHeartBeat& getInstance();

    void shutdown();
    void postToAll(int timestamp = 0);
    void relayMessage(const uint256& hash, int timestamp);
    void recieveMessage(const uint256& hash, int timestamp);
    void forgetMessage(const uint256& hash);

    bool checkMessageIsRecieved(const uint256& hash);
    int getMessageTimestamp(const uint256& hash) const;
    int getLastMessageTimestamp() const;

private:
    CHeartBeat();
    ~CHeartBeat();

private:
    static std::mutex mutex;
    static CHeartBeat * instance;
    std::map<uint256, int> messages;
};

#endif
