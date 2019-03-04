#include <gtest/gtest.h>
#include <chainparamsbase.h>

#include "../masternodes/dpos_voter.h"

namespace
{

using UniElement = boost::variant<CTransaction, CBlock, dpos::CTxVote, dpos::CRoundVote>;

using Uni = std::vector<UniElement>;

Uni& operator+=(Uni& l, const Uni& r)
{
    std::copy(r.begin(), r.end(), std::back_inserter(l));
    return l;
}

}

class StormTestSuit
{
public:
    const int MAX_PROBABILITY = 50000;

    int probabilityOfBlockGeneration = MAX_PROBABILITY / 100;
    int probabilityOfDisconnection = MAX_PROBABILITY / 1000;

    unsigned int seed = 0;
    int randRange = 1;

    std::vector<dpos::CDposVoter> voters;

    using Tick = int;
    using VoterId = size_t;
    std::set<std::pair<TxId, TxId> > conflicts;

    Tick disconnectionPeriod = 5;

    Tick minTick = 2;
    Tick maxTick = 100;

    void addConflict(TxId t1, TxId t2)
    {
        conflicts.emplace(t1, t2);
        conflicts.emplace(t2, t1);
    }

    std::vector<CTransaction> txs;

    /**
     *
     * @return ticks passed
     */
    Tick run()
    {
        LogPrintf("---- start with %d voters, %d txs \n", voters.size(), txs.size());

        VotingTrace trace{};

        // schedule txs
        for (const auto& tx : txs) {
            for (VoterId voterId = 0; voterId < voters.size(); voterId++) {
                const Tick scheduledTick = rand_r(&seed) % randRange;

                trace[scheduledTick][voterId].emplace_back(tx);
            }
        }

        // evaluate the schedule
        bool tickEmpty = false;
        boost::optional<dpos::CBlockToSubmit> blockToSubmit{};
        Tick t = 0;
        for (; (!blockToSubmit || !tickEmpty || t < minTick) && t <= maxTick; t++) {
            blockToSubmit = {};
            tickEmpty = true;
            size_t msgsIn = 0;
            size_t msgsOut = 0;

            for (VoterId voterId = 0; voterId < voters.size(); voterId++) {
                LogPrintf("---- voter#%d: apply %d messages \n", voterId, trace[t][voterId].size());
                msgsIn += trace[t][voterId].size();

                // apply scheduled messages
                auto res = applyUni(voters[voterId], trace[t][voterId]);
                auto& uniMsgs = res.first;
                if (!uniMsgs.empty())
                    tickEmpty = false;

                if (res.second)
                    blockToSubmit = res.second;

                msgsOut += uniMsgs.size();
                LogPrintf("---- voter#%d: sent %d messages, blocks to submit: %d \n\n",
                          voterId,
                          uniMsgs.size(),
                          (int) !!res.second);

                // generate new vice block, according to current state of the voter
                if ((rand_r(&seed) % MAX_PROBABILITY) < probabilityOfBlockGeneration) {
                    CBlock newViceBlock{};
                    newViceBlock.nRound = voters[voterId].getCurrentRound();
                    newViceBlock.nTime = seed;
                    newViceBlock.hashPrevBlock = uint256S("0xB101");

                    const auto committedTxs = voters[voterId].listCommittedTxs();
                    newViceBlock.vtx.reserve(committedTxs.size());
                    for (const auto& pair : committedTxs) {
                        newViceBlock.vtx.emplace_back(pair.second);
                    }

                    LogPrintf("---- voter#%d: generate vice-block with %d txs, at round %d \n\n",
                              voterId,
                              newViceBlock.vtx.size(),
                              newViceBlock.nRound);

                    uniMsgs.emplace_back(newViceBlock);
                }

                // schedule new messages
                for (const auto& item : uniMsgs) {
                    for (VoterId voterIdToSchedule = 0; voterIdToSchedule < voters.size(); voterIdToSchedule++) {
                        const Tick scheduledTick = t + 1 + rand_r(&seed) % randRange;

                        trace[scheduledTick][voterIdToSchedule].emplace_back(item);
                    }
                }

                // disconnect MN
                if ((rand_r(&seed) % MAX_PROBABILITY) < probabilityOfDisconnection) {
                    // reschedule all the items in input voting trace after this tick, so this MN will receive the messages later
                    // Was: tick3 = [vote0, block2, tx1], tick4 = [vote1]
                    // Became: tick20 = [vote0, block2, tx1, vote1]
                    for (Tick disconnectedTick = t + 1; disconnectedTick < (t + 1 + disconnectionPeriod);
                         disconnectedTick++) {
                        trace[t + 1 + disconnectionPeriod][voterId] += trace[disconnectedTick][voterId];
                    }
                }
            }
            LogPrintf("---- end of tick %d, input msgs %d, output msgs %d, blockToSubmit: %d, tickEmpty: %d \n\n\n\n",
                      t,
                      msgsIn,
                      msgsOut,
                      (int) !!blockToSubmit,
                      (int) tickEmpty);
        }

        return t;
    }

    dpos::CDposVoter::Callbacks getValidationCallbacks() const
    {
        dpos::CDposVoter::Callbacks callbacks;
        auto validateTxs = [=](const std::map<TxIdSorted, CTransaction>& txsIn)
        {
            for (const auto& tx1_p : txsIn) {
                const TxId txid1 = tx1_p.second.GetHash();

                for (const auto& tx2_p : txsIn) {
                    const TxId txid2 = tx2_p.second.GetHash();

                    if (conflicts.count(std::pair<TxId, TxId>{txid1, txid2}) != 0) {
                        LogPrintf("txs are conflicted: %s and %s \n", txid1.GetHex(), txid2.GetHex());
                        return false;
                    }
                }
            }

            return true;
        };
        callbacks.validateTx = [](const CTransaction&)
        {
            return true;
        };
        callbacks.validateTxs = validateTxs;
        callbacks.validateBlock =
            [=](const CBlock& b, const std::map<TxIdSorted, CTransaction>& _committedTxs, bool checkTxs)
            {
                if (!checkTxs)
                    return true;

                std::map<TxIdSorted, CTransaction> committedTxs = _committedTxs;
                std::map<TxIdSorted, CTransaction> committedTxs_block{};
                std::map<TxIdSorted, CTransaction> committedTxs_all = _committedTxs;

                for (const auto& tx : b.vtx) {
                    committedTxs_block.emplace(tx.GetDposSortingHash(), tx);
                    committedTxs_all.emplace(tx.GetDposSortingHash(), tx);
                }
                // check that block contains all the committed txs
                for (const auto& tx_p : committedTxs) {
                    if (committedTxs_block.count(tx_p.first) == 0) {
                        LogPrintf("vice block %s doesn't contain committed tx %s \n", b.GetHash().GetHex(), tx_p.first.GetHex());
                        return false;
                    }
                }

                // check that txs are not controversial
                return callbacks.validateTxs(committedTxs_all);
            };
        callbacks.allowArchiving = [](BlockHash votingId)
        {
            return true;
        };

        return callbacks;
    }

private:
    using VotingTrace = std::map<Tick, std::map<VoterId, Uni> >;

    std::pair<Uni, boost::optional<dpos::CBlockToSubmit> > toUni(const dpos::CDposVoter::Output& in) const
    {
        Uni res;
        for (const auto& vote : in.vRoundVotes) {
            res.emplace_back(vote);
        }
        for (const auto& vote : in.vTxVotes) {
            res.emplace_back(vote);
        }

        // assume that errors are testing mistake
        if (!in.vErrors.empty()) {
            throw std::logic_error{in.vErrors[0]};
        }

        return {res, in.blockToSubmit};
    }

    class UniVisitor: public boost::static_visitor<>
    {
    public:
        dpos::CDposVoter* pvoter = nullptr;
        dpos::CDposVoter::Output out;

        void operator()(const CTransaction& tx)
        {
            out += pvoter->applyTx(tx);
        }
        void operator()(const CBlock& viceBlock)
        {
            out += pvoter->applyViceBlock(viceBlock);
        }
        void operator()(const dpos::CTxVote& vote)
        {
            out += pvoter->applyTxVote(vote);
        }
        void operator()(const dpos::CRoundVote& vote)
        {
            out += pvoter->applyRoundVote(vote);
        }
    };

    std::pair<Uni, boost::optional<dpos::CBlockToSubmit> > applyUni(dpos::CDposVoter& voter, const Uni& in) const
    {
        UniVisitor visitor;
        visitor.pvoter = &voter;

        for (const auto& item : in)
            item.apply_visitor(visitor);

        return toUni(visitor.out);
    }

};

/// all the txs are not conflicting, almost no disconnections, instant ping
TEST(dPoS_storm, OptimisticStorm)
{
    StormTestSuit suit{};

    // create dummy txs
    for (int i = 0; i < 10; i++) {
        CMutableTransaction mtx;
        mtx.fInstant = true;
        mtx.fOverwintered = true;
        mtx.nVersion = 4;
        mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
        mtx.nExpiryHeight = 0;
        mtx.nLockTime = i;

        suit.txs.emplace_back(mtx);
        LogPrintf("tx%d: %s \n", i, mtx.GetHash().GetHex());
    }

    // create voters
    BlockHash tip = uint256S("0xB101");
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters.emplace_back(suit.getValidationCallbacks());
    }
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters[i].minQuorum = 23;
        suit.voters[i].numOfVoters = 32;
        suit.voters[i].updateTip(tip);
        suit.voters[i].setVoting(true, ArithToUint256(arith_uint256{i}));
    }

    suit.maxTick = 5;
    suit.probabilityOfBlockGeneration = suit.MAX_PROBABILITY / 10;
    ASSERT_LE(suit.run(), suit.maxTick);

    ASSERT_EQ(suit.voters[0].listCommittedTxs().size(), 10);
}

/// 2 pairs of conflicted txs, frequent disconnections, big ping, a lot of vice-blocks
TEST(dPoS_storm, PessimisticStorm)
{
    StormTestSuit suit{};

    // create dummy txs
    for (int i = 0; i < 4; i++) {
        CMutableTransaction mtx;
        mtx.fInstant = true;
        mtx.fOverwintered = true;
        mtx.nVersion = 4;
        mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
        mtx.nExpiryHeight = 0;
        mtx.nLockTime = i;

        suit.txs.emplace_back(mtx);
        LogPrintf("tx%d: %s \n", i, mtx.GetHash().GetHex());
    }

    suit.addConflict(suit.txs[0].GetHash(), suit.txs[1].GetHash());

    // create voters
    BlockHash tip = uint256S("0xB101");
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters.emplace_back(suit.getValidationCallbacks());
    }
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters[i].minQuorum = 23;
        suit.voters[i].numOfVoters = 32;
        suit.voters[i].updateTip(tip);
        suit.voters[i].setVoting(true, ArithToUint256(arith_uint256{i}));
    }

    suit.randRange = 25;
    suit.maxTick = 500;
    suit.minTick = suit.randRange + 1;
    suit.probabilityOfBlockGeneration = suit.MAX_PROBABILITY / 2000;
    suit.probabilityOfDisconnection = suit.MAX_PROBABILITY / 2000;
    ASSERT_LE(suit.run(), suit.maxTick);

    ASSERT_EQ(suit.voters[0].listCommittedTxs().size(), 2);
}

/// 2 pairs of conflicted txs, a lot of not conflixted txs, small number of vice-blocks, rare disconnections, medium ping
TEST(dPoS_storm, RealisticStorm)
{
    StormTestSuit suit{};

    // create dummy txs
    for (int i = 0; i < 50; i++) {
        CMutableTransaction mtx;
        mtx.fInstant = true;
        mtx.fOverwintered = true;
        mtx.nVersion = 4;
        mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
        mtx.nExpiryHeight = 0;
        mtx.nLockTime = i;

        suit.txs.emplace_back(mtx);
        LogPrintf("tx%d: %s \n", i, mtx.GetHash().GetHex());
    }

    suit.addConflict(suit.txs[0].GetHash(), suit.txs[1].GetHash());
    suit.addConflict(suit.txs[2].GetHash(), suit.txs[3].GetHash());

    // create voters
    BlockHash tip = uint256S("0xB101");
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters.emplace_back(suit.getValidationCallbacks());
    }
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters[i].minQuorum = 23;
        suit.voters[i].numOfVoters = 32;
        suit.voters[i].updateTip(tip);
        suit.voters[i].setVoting(true, ArithToUint256(arith_uint256{i}));
    }

    suit.randRange = 10;
    suit.maxTick = 150;
    suit.minTick = suit.randRange + 1;
    suit.probabilityOfBlockGeneration = suit.MAX_PROBABILITY / 5000;
    suit.probabilityOfDisconnection = suit.MAX_PROBABILITY / 50000;
    ASSERT_LE(suit.run(), suit.maxTick);

    ASSERT_LE(suit.voters[0].listCommittedTxs().size(), 48);
    ASSERT_GE(suit.voters[0].listCommittedTxs().size(), 46);
}
