// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODES_DPOS_H
#define BITCOIN_MASTERNODES_DPOS_H

#include "../uint256.h"
#include "../serialize.h"
#include "../primitives/block.h"
#include <map>

class CKey;
class CTransaction;
class CValidationInterface;

class CVoteSignature : public std::vector<unsigned char>
{
public:
    CVoteSignature();
    explicit CVoteSignature(const std::vector<unsigned char>& vch);

    std::string ToHex() const;

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        s.write(reinterpret_cast<const char*>(data()), size());
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        s.read(reinterpret_cast<char*>(data()), size());
    }
};

class CVoteChoice
{
public:
    enum { decisionPass = -1, decisionNo, decisionYes };

    uint256 hash;
    int8_t decision;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(hash);
        READWRITE(decision);
    }
};

class CTransactionVote
{
public:
    uint256 tipBlockHash;
    uint16_t roundNumber;
    std::vector<CVoteChoice> choices;
    CVoteSignature authSignature;

    CTransactionVote();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(tipBlockHash);
        READWRITE(roundNumber);
        READWRITE(choices);
        READWRITE(authSignature);
    }

    bool IsNull() const;
    void SetNull();

    uint256 GetHash() const;
    uint256 GetSignatureHash() const;

    bool containsTransaction(const CTransaction& transaction) const;
};

class CProgenitorVote
{
public:
    uint256 tipBlockHash;
    uint16_t roundNumber;
    CVoteChoice choice;
    CVoteSignature authSignature;

    CProgenitorVote();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(tipBlockHash);
        READWRITE(roundNumber);
        READWRITE(choice);
        READWRITE(authSignature);
    }

    bool IsNull() const;
    void SetNull();

    uint256 GetHash() const;
    uint256 GetSignatureHash() const;
};

class CTransactionVoteTracker
{
public:
    static CTransactionVoteTracker& getInstance();

    void vote(const CTransaction& transaction, const CKey& operatorKey);
    void post(const CTransactionVote& vote);
    void relay(const CTransactionVote& vote);
    bool recieve(const CTransactionVote& vote, bool isMe);
    const CTransactionVote* getReceivedVote(const uint256& hash);
    std::vector<CTransactionVote> listReceivedVotes();

protected:
    std::map<uint256, CTransactionVote>* recievedVotes;

private:
    CTransactionVoteTracker();
    const CTransactionVote* findMyVote(const CKey& key, const CTransaction& transaction);
    bool checkVoteIsConvenient(const CTransactionVote& vote);
};

class CProgenitorVoteTracker
{
public:
    static CProgenitorVoteTracker& getInstance();

    void post(const CProgenitorVote& vote);
    void relay(const CProgenitorVote& vote);
    bool recieve(const CProgenitorVote& vote, bool isMe);
    const CProgenitorVote* findMyVote(const CKey& key);
    const CProgenitorVote* getReceivedVote(const uint256& hash);
    std::vector<CProgenitorVote> listReceivedVotes();

protected:
    std::map<uint256, CProgenitorVote>* recievedVotes;

private:
    CProgenitorVoteTracker();
    const CBlock* findProgenitorBlock(const uint256& dposBlockHash);
    bool checkVoteIsConvenient(const CProgenitorVote& vote);
};

class CProgenitorBlockTracker
{
public:
    static CProgenitorBlockTracker& getInstance();

    bool vote(const CBlock& progenitorBlock, const CKey& operatorKey);
    void post(const CBlock& pblock);
    void relay(const CBlock& pblock);
    bool recieve(const CBlock& pblock, bool isMe);
    const CBlock* getReceivedBlock(const uint256& hash);
    std::vector<CBlock> listReceivedBlocks();

protected:
    std::map<uint256, CBlock>* recievedBlocks;

private:
    CProgenitorBlockTracker();
    bool checkBlockIsConvenient(const CBlock& block);
};

namespace dpos
{
    bool checkIsActive();
    CValidationInterface * getValidationListener();
}

#endif // BITCOIN_MASTERNODES_DPOS_H
