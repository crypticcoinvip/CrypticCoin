// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERNODES_DPOS_VOTER_H
#define MASTERNODES_DPOS_VOTER_H

#include "dpos_p2p_messages.h"
#include "dpos_types.h"
#include "masternodes.h"
#include "../primitives/block.h"
#include <type_traits>

namespace dpos
{

struct CDposVote
{
    CMasternode::ID voter;
    Round nRound = 0;
    BlockHash tip;
    CVoteChoice choice;

    uint256 GetHash() const {
        CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
        ss << voter
           << tip
           << nRound
           << choice;
        return Hash(ss.begin(), ss.end());
    }
};
bool operator==(const CDposVote& l, const CDposVote& r);
bool operator!=(const CDposVote& l, const CDposVote& r);

class CTxVote : public CDposVote {};
bool operator==(const CTxVote& l, const CTxVote& r);
bool operator!=(const CTxVote& l, const CTxVote& r);

class CRoundVote : public CDposVote {};
bool operator==(const CRoundVote& l, const CRoundVote& r);
bool operator!=(const CRoundVote& l, const CRoundVote& r);

struct CTxVotingDistribution
{
    size_t pro = 0; //< yes
    size_t totus() const; //< total
};

struct CRoundVotingDistribution
{
    std::map<BlockHash, size_t> pro; //< yes
    size_t totus() const; //< total
};

/**
 * When voter finds that a vice-block is approved, it returns this object (as a part of CDposVoterOutput)
 */
struct CBlockToSubmit
{
    CBlock block;
    std::vector<CMasternode::ID> vApprovedBy;
};

/**
 * Result of applying a new message to the voter agent.
 * Voter agent returns a new messages, which should be broadcasted to other agents.
 */
struct CDposVoterOutput
{
    std::vector<CTxVote> vTxVotes;
    std::vector<CRoundVote> vRoundVotes;
    std::vector<TxId> vTxReqs;
    std::vector<BlockHash> vViceBlockReqs;
    boost::optional<CBlockToSubmit> blockToSubmit;
    std::vector<std::string> vErrors;

    bool empty() const;

    CDposVoterOutput& operator+=(const CDposVoterOutput& r);
    CDposVoterOutput operator+(const CDposVoterOutput& r);
};

/**
 * @brief implements dPoS voting mechanism as a black box
 * NOT THREAD-SAVE! External sync is expected.
 * It's essential that this component has minimal dependencies on other systems, like blockchain or p2p messaging. It's needed to implement an efficient unit testing.
 */
class CDposVoter
{
public:
    using PreValidateTxF = std::function<bool(const CTransaction&, uint32_t txExpiringSoonThreshold)>;
    using ValidateTxF = std::function<bool(const CTransaction&)>;
    /// block to validate, fJustCheckPoW
    using ValidateBlockF = std::function<bool(const CBlock&, bool)>;
    /// @return true if saving inventories from this block is allowed
    using AllowArchivingF = std::function<bool(const BlockHash&)>;
    using GetPrevBlockF = std::function<BlockHash(const BlockHash&)>;
    using Output = CDposVoterOutput;

    static constexpr uint32_t VOTING_MEMORY = 4;
    static constexpr uint32_t Z_OUTPUT_INDEX = std::numeric_limits<uint32_t>::max() - 1;

    /**
    * State of the voting at a specific block hash
    */
    struct VotingState
    {
        std::map<CMasternode::ID, std::vector<CTxVote> > mnTxVotes;
        std::map<TxId, std::map<CMasternode::ID, CTxVote> > txVotes;
        std::map<Round, std::map<CMasternode::ID, CRoundVote> > roundVotes;

        std::map<BlockHash, CBlock> viceBlocks;

        bool isNull() const
        {
            return mnTxVotes.empty() &&
                   txVotes.empty() &&
                   roundVotes.empty() &&
                   viceBlocks.empty();
        }
    };

    /**
    * Used to access blockchain
    */
    struct Callbacks
    {
        ValidateTxF validateTx;
        PreValidateTxF preValidateTx;
        ValidateBlockF validateBlock;
        AllowArchivingF allowArchiving;
        GetPrevBlockF getPrevBlock;
    };

    mutable std::map<BlockHash, VotingState> v;
    mutable std::map<TxId, CTransaction> txs;
    mutable std::multimap<COutPoint, TxId> pledgedInputs; // used inputs -> tx. Only for voted txs

    std::map<TxId, CTransaction>::iterator pruneTx(std::map<TxId, CTransaction>::iterator tx_it);

    size_t minQuorum;
    size_t numOfVoters;

    size_t maxNotVotedTxsToKeep;
    size_t maxTxVotesFromVoter;

    /// @param world - blockchain callbacks
    explicit CDposVoter(Callbacks world);

    /**
    * @param amIvoter is true if voting is enabled and I'm an active operator, member of the team
    * @param me is ID of current masternode
    */
    void setVoting(bool amIvoter,
                   CMasternode::ID me);

    /// @param tip - current best block
    void updateTip(BlockHash tip);

    Output applyViceBlock(const CBlock& viceBlock);
    Output applyTx(const CTransaction& tx);
    Output applyTxVote(const CTxVote& vote);
    Output applyRoundVote(const CRoundVote& vote);

    Output requestMissingTxs();

    Output doRoundVoting();
    /**
     * Submit if valid vice-block with enough votes
     */
    Output tryToSubmitBlock(BlockHash viceBlockId, Round nRound);
    Output doTxsVoting();

    const BlockHash & getTip() const;
    bool checkAmIVoter() const;


    struct CommittedTxs {
        std::vector<CTransaction> txs;
        std::set<TxId> missing;
    };
    CommittedTxs listCommittedTxs(BlockHash vot) const;

    bool isCommittedTx(const TxId& txid, BlockHash vot, Round nRound) const;
    bool isTxApprovedByMe(const TxId& txid, BlockHash vot, Round nRound) const;

    CTxVotingDistribution calcTxVotingStats(TxId txid, BlockHash vot, Round nRound) const;
    CRoundVotingDistribution calcRoundVotingStats(BlockHash vot, Round nRound) const;

    bool verifyVotingState() const;

    // for miner
    Round getLowestNotOccupiedRound() const;
    std::set<COutPoint> betterToAvoid() const;

    // for the wallet
    /**
     * Check that tx cannot be committed, due to already known committed txs
     */
    bool checkTxNotCommittable(TxId txid, BlockHash vot) const;

    static std::vector<COutPoint> getInputsOf(const CTransaction& tx);
    static std::set<COutPoint> getInputsOf_set(const CTransaction& tx);

protected:
    Output misbehavingErr(const std::string& msg) const;
    Output voteForTx(const CTransaction& tx);

    /**
     * @return transaction which had YES/NO vote from me, from any round. And transaction which had PASS vote from me in this round.
     */
    bool wasVotedByMe_tx(TxId txid, BlockHash vot, Round nRound) const;
    bool wasVotedByMe_round(BlockHash vot, Round nRound) const;

    bool txHasAnyVote(TxId txid, BlockHash vot) const;
    bool wasViceBlockLost(BlockHash votHash) const;
    bool wasTxLost(TxId txid, BlockHash vot) const;

    struct MyTxsPledge
    {
        std::map<COutPoint, TxId> assignedInputs;
        std::multimap<COutPoint, TxId> vblockAssignedInputs;
        size_t votedTxsSerializeSize;

        std::set<TxId> missingTxs;
        std::set<BlockHash> missingViceBlocks;
    };
    /**
     * @return transactions which had YES vote from me during last VOTING_MEMORY blocks, txs which where included into vice-blocks I approved.
     */
    MyTxsPledge myTxsPledge() const;
    void myTxsPledgeForBlock(MyTxsPledge& res, BlockHash vot) const;

    Output ensurePledgeItemsNotMissing(const std::string& methodName, MyTxsPledge& pledge) const;

    boost::optional<CRoundVote> voteForViceBlock(const CBlock& viceBlock, MyTxsPledge& pledge) const;

protected:
    CMasternode::ID me;
    BlockHash tip;
    Callbacks world;
    bool amIvoter = false;
};

}

#endif //MASTERNODES_DPOS_VOTER_H
