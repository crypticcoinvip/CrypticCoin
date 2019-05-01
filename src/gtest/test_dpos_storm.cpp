#include <gtest/gtest.h>
#include <chainparamsbase.h>

#include "../masternodes/dpos_voter.h"

namespace
{

using UniElement = boost::variant<CTransaction, CBlock, dpos::CTxVote, dpos::CRoundVote>;

using UniV = std::vector<UniElement>;

UniV& operator+=(UniV& l, const UniV& r)
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

    Tick maxTick = 100;

    void printTxs() const
    {
        LogPrintf("Instant txs:\n");
        for (const auto& tx : txs) {
            LogPrintf("%s\n", tx.GetHash().GetHex());
        }
        LogPrintf("Not Instant txs:\n");
        for (const auto& tx : txs_nonInstant) {
            LogPrintf("%s\n", tx.GetHash().GetHex());
        }
    }

    void addConflict(CTransaction& tx1, CTransaction& tx2, bool transperent) const
    {
        CMutableTransaction tx1_m{tx1};
        CMutableTransaction tx2_m{tx2};

        if (transperent) {
            tx1_m.vin.emplace_back();
            tx1_m.vin.back().prevout.n = (uint32_t) rand();
            tx1_m.vin.back().prevout.hash = uint256S(std::to_string(rand()));

            tx2_m.vin.emplace_back(tx1_m.vin.back());
        } else {
            tx1_m.vShieldedSpend.emplace_back();
            tx1_m.vShieldedSpend.back().zkproof = libzcash::GrothProof{}; // avoid profiler warnings
            tx1_m.vShieldedSpend.back().spendAuthSig = SpendDescription::spend_auth_sig_t{}; // avoid profiler warnings
            tx1_m.vShieldedSpend.back().nullifier = uint256S(std::to_string(rand()));

            tx2_m.vShieldedSpend.emplace_back(tx1_m.vShieldedSpend.back());
        }

        tx1 = {tx1_m};
        tx2 = {tx2_m};
    }

    std::vector<CTransaction> txs;
    std::vector<CTransaction> txs_nonInstant;

    std::map<TxId, CTransaction> minedTxs;
    std::set<COutPoint> usedInputs;

    std::map<BlockHash, int> blockToHeight;
    std::map<int, BlockHash> heightToBlock;

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

        auto world = getValidationCallbacks();

        // evaluate the schedule
        Tick foundBlockToSubmitAt = -1;
        boost::optional<dpos::CBlockToSubmit> blockToSubmit{};
        Tick t = 0;

        // after block is found, wait for {randRange} to check that there'll be no new different block
        for (; !((foundBlockToSubmitAt >= 0 && (t - foundBlockToSubmitAt) >= 3 * randRange) || t > maxTick); t++) {
            size_t msgsIn = 0;
            size_t msgsOut = 0;

            for (VoterId voterId = 0; voterId < voters.size(); voterId++) {
                LogPrintf("---- voter#%d: apply %d messages \n", voterId, trace[t][voterId].size());
                msgsIn += trace[t][voterId].size();

                // apply scheduled messages
                auto res = applyUni(voters[voterId], trace[t][voterId]);
                auto& uniMsgs = res.first;

                if (t == 0) { // initially, call doTxsVoting + doRoundVoting
                    auto initialJobs = toUni(voters[voterId].doTxsVoting() + voters[voterId].doRoundVoting());
                    std::copy(initialJobs.first.begin(), initialJobs.first.end(), std::back_inserter(uniMsgs));
                }

                if (res.second && blockToSubmit && res.second->block.GetHash() != blockToSubmit->block.GetHash()) {
                    LogPrintf("---- voter#%d: block finality failed, at least 2 blocks have won \n");
                    return maxTick + 2;
                }
                if (res.second) {
                    foundBlockToSubmitAt = t;
                    blockToSubmit = res.second;
                }

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
                    newViceBlock.hashPrevBlock = voters[voterId].getTip();

                    const auto committedTxs = voters[voterId].listCommittedTxs(voters[voterId].getTip(), 1, dpos::CDposVoter::GUARANTEES_MEMORY).txs;
                    for (const auto& tx : committedTxs) {
                        if (world.validateTx(tx))
                            newViceBlock.vtx.emplace_back(tx);
                    }
                    for (const auto& tx : txs_nonInstant) {
                        if (world.validateTx(tx) && !excludeTxFromBlock_miner(voters[voterId], tx))
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

                // decrease skipBlocksTimer/noVotingTimer until it's 0. skipBlocksTimer is decreasing 5 times faster
                if (voters[voterId].skipBlocksTimer > 0) {
                    voters[voterId].skipBlocksTimer -= 5;
                }
                if (voters[voterId].skipBlocksTimer < 0) {
                    voters[voterId].skipBlocksTimer = 0;
                }
                if (voters[voterId].noVotingTimer > 0) {
                    voters[voterId].noVotingTimer--;
                }

                if (!voters[voterId].verifyVotingState()) {
                    LogPrintf("---- voter#%d: verifyVotingState() failed \n",
                              voterId);
                    return maxTick + 3;
                }
            }
            LogPrintf("---- end of tick %d, input msgs %d, output msgs %d, blockToSubmit: %d \n\n\n\n",
                      t,
                      msgsIn,
                      msgsOut,
                      (int) !!blockToSubmit);
        }

        if (blockToSubmit) {
            const auto committedTxs = voters[0].listCommittedTxs(voters[0].getTip(), 1, dpos::CDposVoter::GUARANTEES_MEMORY);
            // insert/verify new block
            for (auto& voter : voters) {
                voter.updateTip(blockToSubmit->block.GetHash());
                int height = blockToHeight[blockToSubmit->block.hashPrevBlock];
                blockToHeight[blockToSubmit->block.GetHash()] = height + 1;
                heightToBlock[height + 1] = blockToSubmit->block.GetHash();
            }
            for (const auto& tx : blockToSubmit->block.vtx) {
                if (!minedTxs.emplace(tx.GetHash(), tx).second) {
                    LogPrintf("---- duplicating transaction \n");
                    return maxTick + 4;
                }
                for (const auto& in : dpos::CDposVoter::getInputsOf(tx)) {
                    if (!usedInputs.emplace(in).second) {
                        LogPrintf("---- doublespend \n");
                        return maxTick + 5;
                    }
                }
            }
            // verify instant txs of the block
            for (const auto& txid : committedTxs.missing) {
                if (minedTxs.count(txid) == 0) {
                    LogPrintf("---- not mined missing committed transaction \n");
                    return maxTick + 6;
                }
            }
            for (const auto& tx : committedTxs.txs) {
                if (minedTxs.count(tx.GetHash()) == 0) {
                    LogPrintf("---- not mined committed transaction \n");
                    return maxTick + 7;
                }
            }
        } else {
            LogPrintf("---- block wasn't found \n");
            return maxTick + 404;
        }

        return t;
    }

    dpos::CDposVoter::Callbacks getValidationCallbacks() const
    {
        dpos::CDposVoter::Callbacks callbacks;
        callbacks.validateTx = [&](const CTransaction& tx)
        {
            if (minedTxs.count(tx.GetHash()) > 0)
                return false;
            for (const auto& in : dpos::CDposVoter::getInputsOf(tx)) {
                if (usedInputs.count(in) > 0) {
                    return false;
                }
            }
            return true;
        };
        callbacks.preValidateTx = [](const CTransaction&, uint32_t)
        {
            return true;
        };
        callbacks.validateBlock = [&](const CBlock& b, bool fJustCheckPoW)
        {
            if (fJustCheckPoW)
                return true;
            // checks only that txs are not conflicting with prev. blocks. So non-instant txs shouldn't conflict with themself
            for (const auto& tx : b.vtx) {
                if (minedTxs.count(tx.GetHash()) > 0)
                    return false;
                for (const auto& in : dpos::CDposVoter::getInputsOf(tx)) {
                    if (usedInputs.count(in) > 0) {
                        return false;
                    }
                }
            }
            return true;
        };
        callbacks.allowArchiving = [](BlockHash votingId)
        {
            return true;
        };
        callbacks.getPrevBlock = [&](BlockHash block)
        {
            int height = blockToHeight.at(block);
            if (height == 0)
                return BlockHash{};
            return heightToBlock.at(height - 1);
        };
        callbacks.getTime = [&]() {
            return 1 + randRange * 4; // 4 times greater than the ping should ensure finality
        };

        return callbacks;
    }

private:
    using VotingTrace = std::map<Tick, std::map<VoterId, UniV> >;

    std::pair<UniV, boost::optional<dpos::CBlockToSubmit> > toUni(const dpos::CDposVoter::Output& in) const
    {
        UniV res;
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

    std::pair<UniV, boost::optional<dpos::CBlockToSubmit> > applyUni(dpos::CDposVoter& voter, const UniV& in) const
    {
        UniVisitor visitor;
        visitor.pvoter = &voter;

        for (const auto& item : in)
            item.apply_visitor(visitor);

        return toUni(visitor.out);
    }

    bool excludeTxFromBlock_miner(dpos::CDposVoter& voter, const CTransaction& tx) const
    {
        for (const auto& in : dpos::CDposVoter::getInputsOf(tx)) {
            if (voter.pledgedInputs.count(in) > 0) {
                return true;
            }
        }
        return false;
    }
};

/// all the txs are not conflicting, no disconnections, instant ping
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
    }
    suit.printTxs();

    // create voters
    BlockHash tip = uint256S("0xB101");
    suit.blockToHeight[tip] = 0;
    suit.heightToBlock[0] = tip;

    for (uint64_t i = 0; i < 32u; i++) {
        suit.voters.emplace_back(suit.getValidationCallbacks());
    }
    for (uint64_t i = 0; i < 32u; i++) {
        suit.voters[i].minQuorum = 32;
        suit.voters[i].numOfVoters = 23;
        suit.voters[i].maxTxVotesFromVoter = 60;
        suit.voters[i].maxNotVotedTxsToKeep = 600;
        suit.voters[i].updateTip(tip);
        suit.voters[i].setVoting(true, ArithToUint256(arith_uint256{i}));
    }

    suit.maxTick = 10;
    suit.probabilityOfBlockGeneration = suit.MAX_PROBABILITY / 10;
    suit.probabilityOfDisconnection = 0;
    for (int i = 0; i < 2 * dpos::CDposVoter::GUARANTEES_MEMORY; i++)
        ASSERT_LE(suit.run(), suit.maxTick);

    auto committedTxs = suit.voters[0].listCommittedTxs(tip, 0, 2 * dpos::CDposVoter::GUARANTEES_MEMORY);
    size_t committedNum = committedTxs.txs.size() + committedTxs.missing.size();
    ASSERT_EQ(suit.minedTxs.size(), 10u);
    ASSERT_EQ(committedNum, 10u);
}

/// 2 pairs of conflicted txs, frequent disconnections, big ping, a lot of vice-blocks. 9 mns are down, so 23 mns is jsut enough for consensus
TEST(dPoS_storm, PessimisticStorm)
{
    StormTestSuit suit{};

    // create dummy txs
    for (uint32_t i = 0; i < 8u; i++) {
        CMutableTransaction mtx;
        mtx.fInstant = i < 6u;
        mtx.fOverwintered = true;
        mtx.nVersion = 4;
        mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
        mtx.nExpiryHeight = 0;
        mtx.nLockTime = i;

        if (mtx.fInstant)
            suit.txs.emplace_back(mtx);
        else
            suit.txs_nonInstant.emplace_back(mtx);
    }

    suit.addConflict(suit.txs[0], suit.txs[1], true);
    suit.addConflict(suit.txs[1], suit.txs[2], true);
    suit.addConflict(suit.txs[3], suit.txs_nonInstant[0], true);
    suit.printTxs();

    // create voters
    BlockHash tip = uint256S("0xB101");
    suit.blockToHeight[tip] = 0;
    suit.heightToBlock[0] = tip;

    for (uint64_t i = 0; i < 32; i++) {
        suit.voters.emplace_back(suit.getValidationCallbacks());
    }
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters[i].minQuorum = 23;
        suit.voters[i].numOfVoters = 32;
        suit.voters[i].maxTxVotesFromVoter = 10; // maxTxVotesFromVoter / 2 less than num of instant txs
        suit.voters[i].maxNotVotedTxsToKeep = 60;
        suit.voters[i].updateTip(tip);
        suit.voters[i].setVoting(i < 23, ArithToUint256(arith_uint256{i}));
    }

    suit.randRange = 25;
    suit.maxTick = 1000;
    suit.probabilityOfBlockGeneration = suit.MAX_PROBABILITY / 2; // a LOT of blocks! It's a tough task to ensure liveness here
    suit.probabilityOfDisconnection = suit.MAX_PROBABILITY / 2000;
    for (int i = 0; i < 2 * dpos::CDposVoter::GUARANTEES_MEMORY; i++)
        ASSERT_LE(suit.run(), suit.maxTick);

    auto committedTxs = suit.voters[0].listCommittedTxs(tip, 0, 2 * dpos::CDposVoter::GUARANTEES_MEMORY);
    size_t committedNum = committedTxs.txs.size() + committedTxs.missing.size();
    ASSERT_LE(suit.minedTxs.size(), 5u);
    ASSERT_GE(suit.minedTxs.size(), 3u);
    ASSERT_LE(committedNum, 4u);
    ASSERT_GE(committedNum, 2u);
}

/// 10 mns are down, so any quorum is impossible
TEST(dPoS_storm, ImporssibleStorm)
{
    StormTestSuit suit{};

    // create dummy txs
    for (uint32_t i = 0; i < 6u; i++) {
        CMutableTransaction mtx;
        mtx.fInstant = i < 2u;
        mtx.fOverwintered = true;
        mtx.nVersion = 4;
        mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
        mtx.nExpiryHeight = 0;
        mtx.nLockTime = i;

        if (mtx.fInstant)
            suit.txs.emplace_back(mtx);
        else
            suit.txs_nonInstant.emplace_back(mtx);
    }
    suit.printTxs();

    // create voters
    BlockHash tip = uint256S("0xB101");
    suit.blockToHeight[tip] = 0;
    suit.heightToBlock[0] = tip;

    for (uint64_t i = 0; i < 32; i++) {
        suit.voters.emplace_back(suit.getValidationCallbacks());
    }
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters[i].minQuorum = 23;
        suit.voters[i].numOfVoters = 32;
        suit.voters[i].maxTxVotesFromVoter = 60;
        suit.voters[i].maxNotVotedTxsToKeep = 600;
        suit.voters[i].updateTip(tip);
        suit.voters[i].setVoting(i < 22, ArithToUint256(arith_uint256{i}));
    }

    suit.randRange = 5;
    suit.maxTick = 1000;
    suit.probabilityOfBlockGeneration = suit.MAX_PROBABILITY / 2000;
    for (int i = 0; i < 2; i++)
        ASSERT_EQ(suit.run(), suit.maxTick + 404);

    auto committedTxs = suit.voters[0].listCommittedTxs(tip, 0, 2 * dpos::CDposVoter::GUARANTEES_MEMORY);
    size_t committedNum = committedTxs.txs.size() + committedTxs.missing.size();
    ASSERT_EQ(suit.minedTxs.size(), 0u);
    ASSERT_EQ(committedNum, 0u);
}

/// 2 pairs of conflicted txs, a lot of not conflicted txs, small number of vice-blocks, rare disconnections, medium ping. 7 mns are down
TEST(dPoS_storm, RealisticStorm)
{
    StormTestSuit suit{};

    // create dummy txs
    for (uint32_t i = 0; i < 20u; i++) {
        CMutableTransaction mtx;
        mtx.fInstant = i < 15u;
        mtx.fOverwintered = true;
        mtx.nVersion = 4;
        mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
        mtx.nExpiryHeight = 0;
        mtx.nLockTime = i;

        if (mtx.fInstant)
            suit.txs.emplace_back(mtx);
        else
            suit.txs_nonInstant.emplace_back(mtx);
    }

    suit.addConflict(suit.txs[0], suit.txs[1], false);
    suit.addConflict(suit.txs[0], suit.txs[2], false);
    suit.addConflict(suit.txs[0], suit.txs[3], false);
    suit.addConflict(suit.txs[4], suit.txs[1], false);
    suit.addConflict(suit.txs[4], suit.txs[3], false);

    suit.addConflict(suit.txs[5], suit.txs[6], false);
    suit.addConflict(suit.txs[7], suit.txs[8], false);
    suit.addConflict(suit.txs[8], suit.txs[9], false);

    suit.addConflict(suit.txs[1], suit.txs_nonInstant[0], false);
    suit.addConflict(suit.txs[1], suit.txs_nonInstant[1], false);
    suit.addConflict(suit.txs[2], suit.txs_nonInstant[2], false);
    suit.addConflict(suit.txs[10], suit.txs_nonInstant[3], false);
    suit.printTxs();

    // create voters
    BlockHash tip = uint256S("0xB101");
    suit.blockToHeight[tip] = 0;
    suit.heightToBlock[0] = tip;

    for (uint64_t i = 0; i < 32; i++) {
        suit.voters.emplace_back(suit.getValidationCallbacks());
    }
    for (uint64_t i = 0; i < 32; i++) {
        suit.voters[i].minQuorum = 23;
        suit.voters[i].numOfVoters = 32;
        suit.voters[i].maxTxVotesFromVoter = 60;
        suit.voters[i].maxNotVotedTxsToKeep = 600;
        suit.voters[i].updateTip(tip);
        suit.voters[i].setVoting(i < 25, ArithToUint256(arith_uint256{i}));
    }

    suit.randRange = 10;
    suit.maxTick = 1000;
    suit.probabilityOfBlockGeneration = suit.MAX_PROBABILITY / 1000;
    suit.probabilityOfDisconnection = suit.MAX_PROBABILITY / 10000;
    for (int i = 0; i < 2 * dpos::CDposVoter::GUARANTEES_MEMORY; i++)
        ASSERT_LE(suit.run(), suit.maxTick);

    auto committedTxs = suit.voters[0].listCommittedTxs(tip, 0, 2 * dpos::CDposVoter::GUARANTEES_MEMORY);
    size_t committedNum = committedTxs.txs.size() + committedTxs.missing.size();
    ASSERT_GE(suit.minedTxs.size(), 2u + 4u);
    ASSERT_LE(suit.minedTxs.size(), 20u - 6u);
    ASSERT_GE(committedNum, 4u);
    ASSERT_LE(committedNum, 15u - 4u);
}

// Like RealisticStorm, but with 6 nodes and 200 iterations
TEST(dPoS_storm, ExtraLongStorm)
{
    for (unsigned int seed = 0; seed < 200u; seed++) {
        StormTestSuit suit{};
        suit.seed = seed;

        // create dummy txs
        for (uint32_t i = 0; i < 20u; i++) {
            CMutableTransaction mtx;
            mtx.fInstant = i < 15u;
            mtx.fOverwintered = true;
            mtx.nVersion = 4;
            mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
            mtx.nExpiryHeight = 0;
            mtx.nLockTime = i;

            if (mtx.fInstant)
                suit.txs.emplace_back(mtx);
            else
                suit.txs_nonInstant.emplace_back(mtx);
        }

        suit.addConflict(suit.txs[0], suit.txs[1], false);
        suit.addConflict(suit.txs[0], suit.txs[2], false);
        suit.addConflict(suit.txs[0], suit.txs[3], false);
        suit.addConflict(suit.txs[4], suit.txs[1], false);
        suit.addConflict(suit.txs[4], suit.txs[3], false);

        suit.addConflict(suit.txs[5], suit.txs[6], false);
        suit.addConflict(suit.txs[7], suit.txs[8], false);
        suit.addConflict(suit.txs[8], suit.txs[9], false);

        suit.addConflict(suit.txs[1], suit.txs_nonInstant[0], false);
        suit.addConflict(suit.txs[1], suit.txs_nonInstant[1], false);
        suit.addConflict(suit.txs[2], suit.txs_nonInstant[2], false);
        suit.addConflict(suit.txs[10], suit.txs_nonInstant[3], false);
        suit.printTxs();

        // create voters
        BlockHash tip = uint256S("0xB101");
        suit.blockToHeight[tip] = 0;
        suit.heightToBlock[0] = tip;

        for (uint64_t i = 0; i < 6; i++) {
            suit.voters.emplace_back(suit.getValidationCallbacks());
        }
        for (uint64_t i = 0; i < 6; i++) {
            suit.voters[i].minQuorum = 4;
            suit.voters[i].numOfVoters = 6;
            suit.voters[i].maxTxVotesFromVoter = 60;
            suit.voters[i].maxNotVotedTxsToKeep = 600;
            suit.voters[i].updateTip(tip);
            suit.voters[i].setVoting(i < 5, ArithToUint256(arith_uint256{i}));
        }

        suit.randRange = 5;
        suit.maxTick = 1000;
        suit.probabilityOfBlockGeneration = suit.MAX_PROBABILITY / 100;
        suit.probabilityOfDisconnection = suit.MAX_PROBABILITY / 1000;
        for (int i = 0; i < 2 * dpos::CDposVoter::GUARANTEES_MEMORY; i++)
            ASSERT_LE(suit.run(), suit.maxTick);

        auto committedTxs = suit.voters[0].listCommittedTxs(tip, 0, 2 * dpos::CDposVoter::GUARANTEES_MEMORY);
        size_t committedNum = committedTxs.txs.size() + committedTxs.missing.size();
        ASSERT_GE(suit.minedTxs.size(), 2u + 4u);
        ASSERT_LE(suit.minedTxs.size(), 20u - 6u);
        ASSERT_GE(committedNum, 4u);
        ASSERT_LE(committedNum, 15u - 4u);
    }
}
