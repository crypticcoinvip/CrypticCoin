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
    using GetTimeF = std::function<int64_t()>;
    using Output = CDposVoterOutput;

    // Instant transaction gaurantees are honored for {GUARANTEES_MEMORY} blocks
    // If dPoS won't get disabled for {GUARANTEES_MEMORY - 1} blocks in a row, then instant transactions are safe
    static constexpr uint32_t GUARANTEES_MEMORY = 4;
    // Special marker of ZK nullifiers
    static constexpr uint32_t Z_OUTPUT_INDEX = std::numeric_limits<uint32_t>::max() - 0xbeef;

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
        GetTimeF getTime;
    };

    mutable std::map<BlockHash, VotingState> v;
    mutable std::map<TxId, CTransaction> txs;
    mutable std::multimap<COutPoint, TxId> pledgedInputs; // used inputs -> tx. Only for voted txs

    std::map<TxId, CTransaction>::iterator pruneTx(std::map<TxId, CTransaction>::iterator tx_it);

    size_t minQuorum;
    size_t numOfVoters;

    size_t maxNotVotedTxsToKeep;
    size_t maxTxVotesFromVoter;

    // Primitive timer. When voter creates round vote, it sets this value. Then it should reseted by a controller.
    // Why not a proper timer? Simpler to do unit testing this way.
    int64_t lastRoundVotedTime = 0;
    void resetRoundVotingTimer() {
        lastRoundVotedTime = 0;
    };

    /// @param world - blockchain callbacks
    explicit CDposVoter(Callbacks world);

    /**
    * @param amIvoter is true if voting is enabled and I'm an active operator, member of the team
    * @param me is ID of current masternode
    */
    void setVoting(bool amIvoter, CMasternode::ID me);

    /// @param tip - current best block
    void updateTip(BlockHash tip);

    Output applyViceBlock(const CBlock& viceBlock);
    Output applyTx(const CTransaction& tx);
    Output applyTxVote(const CTxVote& vote);
    Output applyRoundVote(const CRoundVote& vote);

    Output requestMissingTxs() const;

    Output doRoundVoting();
    /**
     * Submit if valid vice-block with enough votes
     */
    Output tryToSubmitBlock(BlockHash viceBlockId, Round nRound);
    Output doTxsVoting();

    const BlockHash & getTip() const;
    bool checkAmIVoter() const;


    struct ApprovedViceBlocks
    {
        std::multimap<COutPoint, TxId> vblockAssignedInputs;
        std::set<BlockHash> missing;
    };
    struct ApprovedTxs
    {
        std::map<COutPoint, TxId> assignedInputs;
        size_t txsSerializeSize;
        std::set<TxId> missing;
    };
    struct CommittedTxs
    {
        std::vector<CTransaction> txs;
        std::set<TxId> missing;
    };
    struct MyPledge
    {
        ApprovedViceBlocks vblocks;
        ApprovedTxs approvedTxs;
        CommittedTxs committedTxs;
    };

    /// @param votingsDeep @param votingsSkip [start - votingsSkip, start - votingsDeep]
    CommittedTxs listCommittedTxs(BlockHash start, uint32_t votingsSkip = 0, uint32_t votingsDeep = 1) const;

    bool isCommittedTx(const TxId& txid, BlockHash vot, Round nRound) const;
    bool isTxApprovedByMe(const TxId& txid, BlockHash vot) const;

    CTxVotingDistribution calcTxVotingStats(TxId txid, BlockHash vot, Round nRound) const;
    CRoundVotingDistribution calcRoundVotingStats(BlockHash vot, Round nRound) const;

    /// perform sanity check
    bool verifyVotingState() const;

    /// called for miner to choose a round for a vice-block
    Round getLowestNotOccupiedRound() const;

    // for the wallet
    /**
     * Check that tx cannot be committed, due to already known committed (and conflicting) tx
     */
    bool checkTxNotCommittable(TxId txid, BlockHash vot) const;

    static std::vector<COutPoint> getInputsOf(const CTransaction& tx);
    static std::set<COutPoint> getInputsOf_set(const CTransaction& tx);

protected:
    Output misbehavingErr(const std::string& msg) const;
    Output voteForTx(const CTransaction& tx);
    boost::optional<CRoundVote> voteForViceBlock(const CBlock& viceBlock, MyPledge& pledge) const;

    /**
     * @return true if transaction had any vote from me during the round.
     */
    bool wasVotedByMe_tx(TxId txid, BlockHash vot, Round nRound) const;
    bool wasVotedByMe_round(BlockHash vot, Round nRound) const;

    bool txHasAnyVote(TxId txid) const;
    bool wasTxLost(TxId txid) const;

    struct PledgeBuilderRanges
    {
        // if deep is 5, and skip is 2, then 2 voting to skip, 3 to iterate
        uint32_t vblocksDeep = GUARANTEES_MEMORY;
        uint32_t approvedTxsDeep = GUARANTEES_MEMORY;
        uint32_t committedTxsSkip = 0;
        uint32_t committedTxsDeep = GUARANTEES_MEMORY;
    };
    MyPledge buildMyPledge(PledgeBuilderRanges ranges) const;
    void buildApprovedTxsPledge(ApprovedTxs& res, BlockHash vot) const;

    struct PledgeRequiredItems
    {
        bool fVblocks = true;
        bool fApprovedTxs = true;
        bool fCommittedTxs = true;
    };
    /// @return true if all the required items are not missing
    bool ensurePledgeItemsNotMissing(PledgeRequiredItems r, const std::string& methodName, MyPledge& pledge, Output& out) const;

    template <typename F>
    void forEachVoting(BlockHash start, uint32_t skip, uint32_t deep, F&& f) const {
        uint32_t i = 0;
        for (BlockHash vot = start; !vot.IsNull() && i < deep; i++, vot = world.getPrevBlock(vot)) {
            if (i < skip)
                continue;
            f(vot);
        }
    }

protected:
    CMasternode::ID me;
    BlockHash tip;
    Callbacks world;
    bool amIvoter = false;
};

}

#endif //MASTERNODES_DPOS_VOTER_H
