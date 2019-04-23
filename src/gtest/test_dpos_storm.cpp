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

    Tick disconnectionPeriod = 5;

    Tick minTick = 2;
    Tick maxTick = 100;

    void addConflict(CTransaction& tx1, CTransaction& tx2, bool transperent)
    {
        CMutableTransaction tx1_m{tx1};
        CMutableTransaction tx2_m{tx2};

        if (transperent) {
            tx1_m.vin.resize(1);
            tx1_m.vin[0].prevout.n = (uint32_t) rand();
            tx1_m.vin[0].prevout.hash = uint256S(std::to_string(rand()));

            tx2_m.vin.resize(2);
            tx2_m.vin[0] = tx1_m.vin[0];
        } else {
            tx1_m.vShieldedSpend.resize(1);
            tx1_m.vShieldedSpend[0].zkproof = libzcash::GrothProof{}; // avoid profiler warnings
            tx1_m.vShieldedSpend[0].spendAuthSig = SpendDescription::spend_auth_sig_t{}; // avoid profiler warnings
            tx1_m.vShieldedSpend[0].nullifier = uint256S(std::to_string(rand()));

            tx2_m.vShieldedSpend.resize(1);
            tx2_m.vShieldedSpend[0] = tx1_m.vShieldedSpend[0];
        }

        tx1 = {tx1_m};
        tx2 = {tx2_m};
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
                    newViceBlock.nRound = voters[voterId].getLowestNotOccupiedRound();
                    newViceBlock.nTime = seed;
                    newViceBlock.hashPrevBlock = uint256S("0xB101");

                    const auto committedTxs = voters[voterId].listCommittedTxs(uint256S("0xB101")).txs;
                    newViceBlock.vtx.reserve(committedTxs.size());
                    for (const auto& tx : committedTxs) {
                        newViceBlock.vtx.emplace_back(tx);
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
        callbacks.validateTx = [](const CTransaction&)
        {
            return true;
        };
        callbacks.preValidateTx = [](const CTransaction&, uint32_t)
        {
            return true;
        };
        callbacks.validateBlock = [](const CBlock& b, bool checkTxs)
        {
            return true;
        };
        callbacks.allowArchiving = [](BlockHash votingId)
        {
            return true;
        };
        callbacks.getPrevBlock = [](BlockHash block)
        {
            return BlockHash{};
        };
        callbacks.getTime = []() {
            return 0;
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
    for (uint32_t i = 0; i < 10u; i++) {
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
    for (uint64_t i = 0; i < 32u; i++) {
        suit.voters.emplace_back(suit.getValidationCallbacks());
    }
    for (uint64_t i = 0; i < 32u; i++) {
        suit.voters[i].minQuorum = 23;
        suit.voters[i].numOfVoters = 32;
        suit.voters[i].maxTxVotesFromVoter = 50;
        suit.voters[i].maxNotVotedTxsToKeep = 100;
        suit.voters[i].updateTip(tip);
        suit.voters[i].setVoting(true, ArithToUint256(arith_uint256{i}));
    }

    suit.maxTick = 5;
    suit.probabilityOfBlockGeneration = suit.MAX_PROBABILITY / 10;
    ASSERT_LE(suit.run(), suit.maxTick);

    ASSERT_EQ(suit.voters[0].listCommittedTxs(tip).txs.size(), 10u);
    ASSERT_EQ(suit.voters[0].listCommittedTxs(tip).missing.size(), 0u);
}

/// 2 pairs of conflicted txs, frequent disconnections, big ping, a lot of vice-blocks. 9 mns are down, so 23 mns is jsut enough
TEST(dPoS_storm, PessimisticStorm)
{
    StormTestSuit suit{};

    // create dummy txs
    for (uint32_t i = 0; i < 4u; i++) {
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

    suit.addConflict(suit.txs[0], suit.txs[1], true);

    // create voters
    BlockHash tip = uint256S("0xB101");
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters.emplace_back(suit.getValidationCallbacks());
    }
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters[i].minQuorum = 23;
        suit.voters[i].numOfVoters = 32;
        suit.voters[i].maxTxVotesFromVoter = 8;
        suit.voters[i].maxNotVotedTxsToKeep = 4;
        suit.voters[i].updateTip(tip);
        suit.voters[i].setVoting(i < 23, ArithToUint256(arith_uint256{i}));
    }

    suit.randRange = 25;
    suit.maxTick = 500;
    suit.minTick = suit.randRange + 1;
    suit.probabilityOfBlockGeneration = suit.MAX_PROBABILITY / 2000;
    suit.probabilityOfDisconnection = suit.MAX_PROBABILITY / 2000;
    ASSERT_LE(suit.run(), suit.maxTick);

    ASSERT_EQ(suit.voters[0].listCommittedTxs(tip).txs.size(), 2u);
    ASSERT_EQ(suit.voters[0].listCommittedTxs(tip).missing.size(), 0u);
}

/// Like PessimisticStorm, but 10 mns are down, so any quorum is impossible
TEST(dPoS_storm, ImporssibleStorm)
{
    StormTestSuit suit{};

    // create dummy txs
    for (uint32_t i = 0; i < 4u; i++) {
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

    suit.addConflict(suit.txs[0], suit.txs[1], true);

    // create voters
    BlockHash tip = uint256S("0xB101");
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters.emplace_back(suit.getValidationCallbacks());
    }
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters[i].minQuorum = 23;
        suit.voters[i].numOfVoters = 32;
        suit.voters[i].maxTxVotesFromVoter = 8;
        suit.voters[i].maxNotVotedTxsToKeep = 4;
        suit.voters[i].updateTip(tip);
        suit.voters[i].setVoting(i < 22, ArithToUint256(arith_uint256{i}));
    }

    suit.randRange = 25;
    suit.maxTick = 1000;
    suit.minTick = suit.randRange + 1;
    suit.probabilityOfBlockGeneration = suit.MAX_PROBABILITY / 2000;
    suit.probabilityOfDisconnection = suit.MAX_PROBABILITY / 2000;
    ASSERT_EQ(suit.run(), suit.maxTick + 1);

    ASSERT_EQ(suit.voters[0].listCommittedTxs(tip).txs.size(), 0u);
    ASSERT_EQ(suit.voters[0].listCommittedTxs(tip).missing.size(), 0u);
}

/// 2 pairs of conflicted txs, a lot of not conflicted txs, small number of vice-blocks, rare disconnections, medium ping. 7 mns are down
TEST(dPoS_storm, RealisticStorm)
{
   StormTestSuit suit{};

    // create dummy txs
    for (uint32_t i = 0; i < 50u; i++) {
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

    suit.addConflict(suit.txs[0], suit.txs[1], false);
    suit.addConflict(suit.txs[2], suit.txs[3], false);
    suit.addConflict(suit.txs[4], suit.txs[5], false);
    suit.addConflict(suit.txs[6], suit.txs[7], false);

    // create voters
    BlockHash tip = uint256S("0xB101");
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters.emplace_back(suit.getValidationCallbacks());
    }
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters[i].minQuorum = 23;
        suit.voters[i].numOfVoters = 32;
        suit.voters[i].maxTxVotesFromVoter = 200;
        suit.voters[i].maxNotVotedTxsToKeep = 50;
        suit.voters[i].updateTip(tip);
        suit.voters[i].setVoting(i < 25, ArithToUint256(arith_uint256{i}));
    }

    suit.randRange = 10;
    suit.maxTick = 500;
    suit.minTick = suit.randRange + 1;
    suit.probabilityOfBlockGeneration = suit.MAX_PROBABILITY / 5000;
    suit.probabilityOfDisconnection = suit.MAX_PROBABILITY / 50000;
    ASSERT_LE(suit.run(), suit.maxTick);

    ASSERT_LE(suit.voters[0].listCommittedTxs(tip).txs.size(), 46u);
    ASSERT_GE(suit.voters[0].listCommittedTxs(tip).txs.size(), 42u);
    ASSERT_EQ(suit.voters[0].listCommittedTxs(tip).missing.size(), 0u);
}
