// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERNODES_H
#define MASTERNODES_H

#include "amount.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/script.h"
#include "serialize.h"
#include "dbwrapper.h"
#include "uint256.h"

//#include <assert.h>
#include <map>
#include <stdint.h>

#include <boost/scoped_ptr.hpp>

static const CAmount MN_ANNOUNCEMENT_FEE = COIN; /// @todo change me
static const CAmount MN_COLLATERAL_AMOUNT = 1000 * COIN;
static const int MAX_DISMISS_VOTES_PER_MN = 20;

enum class MasternodesTxType : unsigned char
{
    None = 0,
    AnnounceMasternode = 'a',
    ActivateMasternode = 'A',
    SetOperatorReward  = 'O',
    DismissVote        = 'V',
    DismissVoteRecall  = 'v',
    CollateralSpent    = 'C'
};

class CMasternode
{
public:
    typedef CKeyID CBitcoinAddress;

    //! Announcement metadata section
    //! Human readable name of this MN, len >= 3, len <= 255
    std::string name;
    //! Owner auth address. Can be used as an ID
    CBitcoinAddress ownerAuthAddress;
    //! Operator auth address. Can be used as an ID
    CBitcoinAddress operatorAuthAddress;
    //! Owner reward address.
    CScript ownerRewardAddress;

//    //! Operator reward metadata section
//    //! Operator reward address. Optional
//    CScript operatorRewardAddress;
//    //! Amount of reward, transferred to <Operator reward address>, instead of <Owner reward address>. Optional
//    CAmount operatorRewardAmount;

    //! Announcement block height
    uint32_t height;
    //! Min activation block height. Computes as <announcement block height> + max(100, number of active masternodes)
    uint32_t minActivationHeight;
    //! Activation block height. -1 if not activated
    int32_t activationHeight;
    //! Deactiavation height (just for trimming DB)
    int32_t deadSinceHeight;

    //! This fields are for transaction rollback (by disconnecting block)
    uint256 activationTx;
    uint256 collateralSpentTx;
    uint256 dismissFinalizedTx;

    uint32_t counterVotesFrom;
    uint32_t counterVotesAgainst;

    //! empty constructor
    CMasternode() {}

    //! constructor helper, runs without any checks
    void FromTx(CTransaction const & tx, int heightIn, std::vector<unsigned char> const & metadata)
    {
        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        ss >> name;
        ss >> ownerAuthAddress;
        ss >> operatorAuthAddress;
        ss >> *(CScriptBase*)(&ownerRewardAddress);

        height = heightIn;
        /// @todo @mn: minActivationHeight = height + std::max(100, GetActiveMasternodesCount());
        minActivationHeight = height + 100;
        activationHeight = -1;
        deadSinceHeight = -1;

        activationTx = uint256();
        collateralSpentTx = uint256();
        dismissFinalizedTx = uint256();

        counterVotesFrom = 0;
        counterVotesAgainst = 0;
    }

    //! construct a CMasternode from a CTransaction, at a given height
    CMasternode(CTransaction const & tx, int heightIn, std::vector<unsigned char> const & metadata)
    {
        FromTx(tx, heightIn, metadata);
    }

    bool IsActive() const
    {
        return activationTx != uint256() && collateralSpentTx == uint256() && dismissFinalizedTx == uint256();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(name);
        READWRITE(ownerAuthAddress);
        READWRITE(operatorAuthAddress);
        READWRITE(*(CScriptBase*)(&ownerRewardAddress));
//        READWRITE(*(CScriptBase*)(&operatorRewardAddress));
//        READWRITE(operatorRewardAmount);
        READWRITE(height);
        READWRITE(minActivationHeight);
        READWRITE(activationHeight);
        READWRITE(deadSinceHeight);

        READWRITE(activationTx);
        READWRITE(collateralSpentTx);
        READWRITE(dismissFinalizedTx);

        READWRITE(counterVotesFrom);
        READWRITE(counterVotesAgainst);
    }

    //! equality test
    friend bool operator==(const CMasternode & a, const CMasternode & b)
    {
        return (a.name == b.name &&
                a.ownerAuthAddress == b.ownerAuthAddress &&
                a.operatorAuthAddress == b.operatorAuthAddress &&
                a.ownerRewardAddress == b.ownerRewardAddress &&
                a.height == b.height &&
                a.minActivationHeight == b.minActivationHeight &&
                a.activationHeight == b.activationHeight &&
                a.deadSinceHeight == b.deadSinceHeight &&
                a.activationTx == b.activationTx &&
                a.collateralSpentTx == b.collateralSpentTx &&
                a.dismissFinalizedTx == b.dismissFinalizedTx &&
                a.counterVotesFrom == b.counterVotesFrom &&
                a.counterVotesAgainst == b.counterVotesAgainst
                );
    }
    friend bool operator!=(const CMasternode & a, const CMasternode & b)
    {
        return !(a == b);
    }
};

//! Active dismiss votes, committed by masternode. len <= MAX_DISMISS_VOTES_PER_MN
class CDismissVote
{
public:
    //! Masternode ID
    uint256 from;

    //! Masternode ID. The block until this vote is active
    uint256 against;

    uint32_t reasonCode;
    //! len <= 255
    std::string reasonDescription;

    //! Deactiavation height (just for trimming DB)
    int32_t deadSinceHeight;
    //! Deactiavation transaction affected by, own or alien (recall vote or finalize dismission)
    uint256 disabledByTx;

    void FromTx(CTransaction const & tx, std::vector<unsigned char> const & metadata)
    {
        from = uint256();
        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        ss >> against;
        ss >> reasonCode;
        ss >> reasonDescription;
        deadSinceHeight = -1;
        disabledByTx = uint256();
    }

    //! construct a CMasternode from a CTransaction, at a given height
    CDismissVote(CTransaction const & tx, std::vector<unsigned char> const & metadata)
    {
        FromTx(tx, metadata);
    }

    //! empty constructor
    CDismissVote() { }

    bool IsActive() const
    {
        return disabledByTx != uint256();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(from);
        READWRITE(against);
        READWRITE(reasonCode);
        READWRITE(reasonDescription);
        READWRITE(deadSinceHeight);
        READWRITE(disabledByTx);
    }

    friend bool operator==(CDismissVote const & a, CDismissVote const & b)
    {
        return (a.from == b.from &&
                a.against == b.against &&
                a.reasonCode == b.reasonCode &&
                a.reasonDescription == b.reasonDescription &&
                a.deadSinceHeight == b.deadSinceHeight &&
                a.disabledByTx == b.disabledByTx
                );
    }

    friend bool operator!=(CDismissVote const & a, CDismissVote const & b)
    {
        return !(a == b);
    }

};

typedef std::map<uint256, CMasternode> CMasternodes;  // nodeId -> masternode object,
typedef std::set<uint256> CActiveMasternodes;         // just nodeId's,
typedef std::map<CKeyID, uint256> CMasternodesByAuth; // for two indexes, owner->nodeId, operator->nodeId

typedef std::map<uint256, CDismissVote> CDismissVotes;
typedef std::multimap<uint256, uint256> CDismissVotesIndex; // just index, from->against or against->from

typedef std::multimap<uint256, std::pair<uint256, MasternodesTxType> > CMasternodesUndo;

class CMasternodesDB;

class CMasternodesView
{
private:
    CMasternodesDB & db;
    boost::scoped_ptr<CDBBatch> currentBatch;
public:
    CMasternodes allNodes;
    CActiveMasternodes activeNodes;
    CMasternodesByAuth nodesByOwner;
    CMasternodesByAuth nodesByOperator;

    CDismissVotes votes;
    CDismissVotesIndex votesFrom;
    CDismissVotesIndex votesAgainst;

    CMasternodesUndo txsUndo;

    CMasternodesView(CMasternodesDB & mndb) : db(mndb) {}
    ~CMasternodesView() {}

    bool HasMasternode(uint256 nodeId)
    {
        return allNodes.find(nodeId) != allNodes.end();
    }

    //! Initial load of all data
    void Load();

    //! Process event of spending collateral. It is assumed that the node exists
    bool OnCollateralSpent(uint256 const & nodeId, uint256 const & txid, uint input, int height);

    bool OnMasternodeAnnounce(uint256 const & nodeId, CMasternode const & node);
    bool OnMasternodeActivate(uint256 const & txid, uint256 const & nodeId, CKeyID const & operatorId, int height);
    bool OnDismissVote(uint256 const & txid, CDismissVote const & vote, CKeyID const & operatorId);
    bool OnDismissVoteRecall(uint256 const & txid, uint256 const & against, CKeyID const & operatorId);

//    bool OnUndoMasternodeAnnounce(uint256 const & nodeId, CMasternode const & node);
//    bool OnUndoMasternodeActivate(uint256 const & txid, uint256 const & nodeId, CKeyID operatorId, int height);
//    bool OnUndoDismissVote(uint256 const & txid, uint256 const & nodeId, CKeyID operatorId, int height)

    void WriteBatch();

private:
    void Clear();
};


//! Checks if given tx is probably one of 'MasternodeTx', returns tx type and serialized metadata in 'data'
MasternodesTxType GuessMasternodeTxType(CTransaction const & tx, std::vector<unsigned char> & metadata);

#endif // MASTERNODES_H
