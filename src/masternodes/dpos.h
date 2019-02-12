// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODES_DPOS_H
#define BITCOIN_MASTERNODES_DPOS_H

#include "../chain.h"
#include <map>

class CKey;
class CValidationInterface;

class CTransactionVote
{
    using Signature = std::vector<unsigned char>;

public:
    uint256 dposBlockHash;
    uint16_t roundNumber;
    uint256 tipBlockHash;
    uint256 progenitorBlockHash;
    Signature authSignature;

    CTransactionVote();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(dposBlockHash);
        READWRITE(roundNumber);
        READWRITE(tipBlockHash);
        READWRITE(progenitorBlockHash);
        READWRITE(authSignature);
    }

    bool IsNull() const;
    void SetNull();

    uint256 GetHash() const;
};

class CProgenitorVote
{
    using Signature = std::vector<unsigned char>;

public:
    uint256 dposBlockHash;
    uint16_t roundNumber;
    uint256 tipBlockHash;
    uint256 progenitorBlockHash;
    Signature authSignature;

    CProgenitorVote();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(dposBlockHash);
        READWRITE(roundNumber);
        READWRITE(tipBlockHash);
        READWRITE(progenitorBlockHash);
        READWRITE(authSignature);
    }

    bool IsNull() const;
    void SetNull();

    uint256 GetHash() const;
};

class CTransactionVoteTracker
{
public:
    static CTransactionVoteTracker& getInstance();

    void post(const CTransactionVote& vote);
    void relay(const CTransactionVote& vote);
    bool recieve(const CTransactionVote& vote, bool isMe);
    const CTransactionVote* getReceivedVote(const uint256& hash);
    std::vector<CTransactionVote> listReceivedVotes();

protected:
    std::map<uint256, CTransactionVote>* recievedVotes;
};

class CProgenitorVoteTracker
{
public:
    static CProgenitorVoteTracker& getInstance();

    void post(const CProgenitorVote& vote);
    void relay(const CProgenitorVote& vote);
    bool recieve(const CProgenitorVote& vote, bool isMe);
    const CProgenitorVote* getReceivedVote(const uint256& hash);
    std::vector<CProgenitorVote> listReceivedVotes();

protected:
    std::map<uint256, CProgenitorVote>* recievedVotes;

private:
    const CBlock* findProgenitorBlock(const uint256& dposBlcokHash);
    bool checkVoteIsConvenient(const CProgenitorVote& vote);
};

class CProgenitorBlockTracker
{
public:
    static CProgenitorBlockTracker& getInstance();

    void post(const CBlock& pblock);
    void relay(const CBlock& pblock);
    bool recieve(const CBlock& pblock, bool isMe);
    const CBlock* getReceivedBlock(const uint256& hash);
    std::vector<CBlock> listReceivedBlocks();

protected:
    std::map<uint256, CBlock>* recievedBlocks;

private:
    const CProgenitorVote* findMyVote(const CKey& key);
    bool voteForProgenitorBlock(const CBlock& progenitorBlock, const CKey& operatorKey);
};

namespace dpos
{
    bool checkIsActive();
    CValidationInterface * getValidationListener();
}

#endif // BITCOIN_MASTERNODES_DPOS_H
