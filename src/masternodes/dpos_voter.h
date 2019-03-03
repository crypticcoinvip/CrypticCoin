// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERNODES_DPOS_VOTER_H
#define MASTERNODES_DPOS_VOTER_H

#include "dpos_p2p_messages.h"
#include "dpos_types.h"
#include "masternodes.h"
#include "../primitives/block.h"

namespace dpos
{

struct CDposVote
{
    CMasternode::ID voter;
    Round nRound = 0;
    BlockHash tip;
    CVoteChoice choice;
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
    size_t contra = 0; //< no
    size_t abstinendi = 0; //< pass
    size_t totus() const; //< total
};

struct CRoundVotingDistribution
{
    std::map<BlockHash, size_t> pro; //< yes
    size_t abstinendi = 0; //< pass
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
    using ValidateTxF = std::function<bool(const CTransaction&)>;
    using ValidateTxsF = std::function<bool(const std::map<TxIdSorted, CTransaction>&)>;
    /// block to validate, dPoS committed txs list, check dPoS txs
    using ValidateBlockF = std::function<bool(const CBlock&, const std::map<TxIdSorted, CTransaction>&, bool)>;
    /// @return true if saving inventories from this block is allowed
    using AllowArchivingF = std::function<bool(const BlockHash&)>;
    using Output = CDposVoterOutput;

    /**
    * State of the voting at a specific block hash
    */
    struct VotingState
    {
        std::map<Round, std::map<TxId, std::map<CMasternode::ID, CTxVote> > > txVotes;
        std::map<Round, std::map<CMasternode::ID, CRoundVote> > roundVotes;

        std::map<BlockHash, CBlock> viceBlocks;

        bool isNull() const
        {
            return txVotes.empty() &&
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
        ValidateTxsF validateTxs;
        ValidateBlockF validateBlock;
        AllowArchivingF allowArchiving;
    };

    mutable std::map<BlockHash, VotingState> v;
    mutable std::map<TxId, CTransaction> txs;

    size_t minQuorum;
    size_t numOfVoters;

    explicit CDposVoter(Callbacks world);

    /**
    *
    * @param tip - current best block
    * @param world - blockchain callbacks
    * @param amIvoter is true if voting is enabled and I'm an active operator, member of the team
    * @param me is ID of current masternode
    */
    void setVoting(bool amIvoter,
                   CMasternode::ID me);

    void updateTip(BlockHash tip);

    Output applyViceBlock(const CBlock& viceBlock);
    Output applyTx(const CTransaction& tx);
    Output applyTxVote(const CTxVote& vote);
    Output applyRoundVote(const CRoundVote& vote);

    void pruneTxVote(const CTxVote& vote);

    Output requestMissingTxs();

    Output doRoundVoting();
    /**
     * Submit if valid vice-block with enough votes
     */
    Output tryToSubmitBlock(BlockHash viceBlockId, Round nRound);
    Output doTxsVoting();
    /**
     * Force to vote PASS during this round, if round wasn't voted before.
     * Called when round didnt come to a consensus/stalemate for a long time.
     */
    Output onRoundTooLong();

    const BlockHash & getTip() const;
    bool checkAmIVoter() const;
    Round getCurrentRound() const;

    std::map<TxIdSorted, CTransaction> listCommittedTxs() const;
    /**
     * @return transaction which had YES vote from me, from any round
     */

    struct ApprovedByMeTxsList
    {
        std::map<TxIdSorted, CTransaction> txs;
        std::set<TxId> missing;
    };
    ApprovedByMeTxsList listApprovedByMe_txs() const;

    bool isCommittedTx(const TxId& txid) const;
    bool isTxApprovedByMe(const TxId& txid) const;

    CTxVotingDistribution calcTxVotingStats(TxId txid, Round nRound) const;
    CRoundVotingDistribution calcRoundVotingStats(Round nRound) const;

protected:
    Output misbehavingErr(const std::string& msg) const;
    Output voteForTx(const CTransaction& tx);

    /**
     * @return transaction which had YES/NO vote from me, from any round. And transaction which had PASS vote from me in this round.
     */
    bool wasVotedByMe_tx(TxId txid, Round nRound) const;
    bool wasVotedByMe_round(Round nRound) const;

    bool atLeastOneViceBlockIsValid(Round nRound) const;
    bool txHasAnyVote(TxId txid) const;
    bool wasTxLost(TxId txid) const;
    bool checkRoundStalemate(const CRoundVotingDistribution& stats) const;
    /**
     * Check that tx cannot be committed, due to already known votes
     */
    bool checkTxNotCommittable(const CTxVotingDistribution& stats) const;

    /**
     * @param nRound specifies a round to calc voting stats (including abstinendi). If 0, then drop abstinendi part
     * @return false if all my txs are either committed or not committable, or if one of my txs is missing
     */
    void filterFinishedTxs(std::map<TxIdSorted, CTransaction>& txs_f, Round nRound) const;
    void filterFinishedTxs(std::map<TxId, CTransaction>& txs_f, Round nRound) const;
protected:
    CMasternode::ID me;
    BlockHash tip;
    Callbacks world;
    bool amIvoter = false;
};

}

#endif //MASTERNODES_DPOS_VOTER_H
