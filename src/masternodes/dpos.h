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
    enum {decisionNo, decisionYes, decisionPass};

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
    uint256 tip;
    uint16_t round;
    std::vector<CVoteChoice> choices;
    CVoteSignature signature;

    CTransactionVote();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(tip);
        READWRITE(round);
        READWRITE(choices);
        READWRITE(signature);
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
    uint256 tip;
    uint16_t round;
    CVoteChoice choice;
    CVoteSignature signature;

    CProgenitorVote();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(tip);
        READWRITE(round);
        READWRITE(choice);
        READWRITE(signature);
    }

    bool IsNull() const;
    void SetNull();

    uint256 GetHash() const;
    uint256 GetSignatureHash() const;
};

class CTransactionVoteTracker
{
protected:
    std::map<uint256, CTransactionVote>* recievedVotes;

public:
    static CTransactionVoteTracker& getInstance();

    void voteForTransaction(const CTransaction& transaction, const CKey& masternodeKey);
    void postVote(const CTransactionVote& voteForTransaction);
    void relayVote(const CTransactionVote& voteForTransaction);
    bool recieveVote(const CTransactionVote& voteForTransaction, bool internal);
    bool findReceivedVote(const uint256& hash, CTransactionVote* voteForTransaction = nullptr);
    std::vector<CTransactionVote> listReceivedVotes();

private:
    CTransactionVoteTracker();
    std::vector<CTransaction> listMyTransactions(const CKey& masternodeKey);
    bool wasVotedByMe(const CKey& masternodeKey, const CTransaction& transaction);
    bool checkVoteIsConvenient(const CTransactionVote& voteForTransaction);
    bool interfereWithList(const CTransaction& transaction, const std::vector<CTransaction>& txList);
    bool exeedSizeLimit(const CTransaction& transaction);
};

class CProgenitorVoteTracker
{
    friend class CProgenitorBlockTracker;

protected:
    std::map<uint256, CProgenitorVote>* recievedVotes;

public:
    static CProgenitorVoteTracker& getInstance();

    void postVote(const CProgenitorVote& vote);
    void relayVote(const CProgenitorVote& vote);
    bool recieveVote(const CProgenitorVote& vote, bool internal);
    bool findReceivedVote(const uint256& hash, CProgenitorVote* vote = nullptr);
    bool hasAnyReceivedVote(int roundNumber, int decision = -1) const;
    bool wasVotedByMe(const CKey& masternodeKey);
    std::vector<CProgenitorVote> listReceivedVotes() const;

private:
    CProgenitorVoteTracker();
    bool checkVoteIsConvenient(const CProgenitorVote& vote);
    bool findProgenitorBlock(const uint256& dposBlockHash, CBlock* block);
};

class CProgenitorBlockTracker
{
protected:
    std::map<uint256, CBlock>* recievedBlocks;

public:
    static CProgenitorBlockTracker& getInstance();

    void postBlock(const CBlock& pblock);
    void relayBlock(const CBlock& pblock);
    bool voteForBlock(const CBlock& progenitorBlock, const CKey& masternodeKey);
    bool recieveBlock(const CBlock& pblock, bool isMe);
    bool findReceivedBlock(const uint256& hash, CBlock* block = nullptr) const;
    bool hasAnyReceivedBlock() const;
    std::vector<CBlock> listReceivedBlocks() const;

    int getCurrentRoundNumber() const;
//    void doRoundVoting(const CKey& masternodeKey);

private:
    CProgenitorBlockTracker();
    bool checkBlockIsConvenient(const CBlock& block);
};

namespace dpos
{
    bool isActive();
    CValidationInterface * getValidationListener();
    std::vector<CTransaction> listCommitedTransactions();
}

#endif // BITCOIN_MASTERNODES_DPOS_H
