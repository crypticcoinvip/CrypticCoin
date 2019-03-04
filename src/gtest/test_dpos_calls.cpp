#include <gtest/gtest.h>
#include <chainparamsbase.h>

#include "../masternodes/dpos_voter.h"

namespace dpos
{

namespace
{

class CDposVoterTesting: public CDposVoter
{
public:
    CDposVoterTesting(Callbacks world)
        : CDposVoter(world)
    {}

    CTxVotingDistribution priv_calcTxVotingStats(TxId txid, Round nRound) const
    {
        return calcTxVotingStats(txid, nRound);
    }
    CRoundVotingDistribution priv_calcRoundVotingStats(Round nRound) const
    {
        return calcRoundVotingStats(nRound);
    }

    bool priv_txHasAnyVote(TxId txid) const
    {
        return txHasAnyVote(txid);
    }
    bool priv_wasTxLost(TxId txid) const
    {
        return wasTxLost(txid);
    }
    bool priv_checkRoundStalemate(const CRoundVotingDistribution& stats) const
    {
        return checkRoundStalemate(stats);
    }
    bool priv_checkTxNotCommittable(const CTxVotingDistribution& stats) const
    {
        return checkTxNotCommittable(stats);
    }

    void priv_filterFinishedTxs(std::map<TxId, CTransaction>& txs_f, Round nRound) const
    {
        filterFinishedTxs(txs_f, nRound);
    }
};

}

void initVoters(std::vector<CMasternode::ID>& masternodeIds,
                std::vector<CDposVoterTesting>& voters,
                BlockHash tip,
                CDposVoterTesting::Callbacks callbacks,
                std::array<TxId, 6>& vTxs,
                std::array<BlockHash, 4>& vViceBlocks)
{
    for (uint64_t i = 0; i < 200; i++) {
        masternodeIds.emplace_back(ArithToUint256(arith_uint256{i}));
        voters.emplace_back(callbacks);
    }

    for (uint64_t i = 0; i < 1; i++) {
        voters[i].minQuorum = 23;
        voters[i].numOfVoters = 32;
        voters[i].updateTip(tip);
        voters[i].setVoting(true, masternodeIds[i]);
    }

    // create 6 dummy txs
    for (int i = 0; i < 6; i++) {
        CMutableTransaction mtx;
        mtx.fInstant = true;
        mtx.fOverwintered = true;
        mtx.nVersion = 4;
        mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
        mtx.nExpiryHeight = 0;
        mtx.nLockTime = i;
        voters[0].txs.emplace(mtx.GetHash(), CTransaction{mtx});

        vTxs[i] = mtx.GetHash();
    }

    // for each of 2 rounds
    for (Round i = 1; i <= 2; i++) {
        // create 2 dummy vice-blocks
        CBlock viceBlock;
        viceBlock.hashPrevBlock = tip;
        viceBlock.nRound = i;

        voters[0].v[tip].viceBlocks.emplace(viceBlock.GetHash(), viceBlock);
        vViceBlocks[(i - 1) * 2] = viceBlock.GetHash();

        viceBlock.nTime = 1;
        voters[0].v[tip].viceBlocks.emplace(viceBlock.GetHash(), viceBlock);
        vViceBlocks[(i - 1) * 2 + 1] = viceBlock.GetHash();
    }

    // votes table:
    // round1
    // tx0: not voted
    // tx1: not voted
    // tx2: not voted
    //
    // tx3: voted yes by mns 0-22
    // tx4: voted no by mns 0-23
    // tx5: not voted
    //
    // vb0: not voted
    // vb1: voted by mns 0-22
    // round pass: voted by mns 22-23
    //
    //
    // round2
    // tx0: voted yes by mns 100-200
    // tx1: voted no by mns 100-200
    // tx2: voted pass by mns 100-200
    //
    // tx3: voted no by mns 22-23
    // tx5: voted pass by mns 0-23
    //
    // vb2: voted by mns 100-200
    // vb3: voted by mns 0-22

    voters[0].v[tip].txVotes[1];
    voters[0].v[tip].txVotes[2];

    for (uint64_t i = 0; i < 23; i++) {
        // Round 1
        CTxVote newVote;
        newVote.nRound = 1;
        newVote.tip = tip;
        newVote.voter = masternodeIds[i];
        newVote.choice.decision = CVoteChoice::Decision::NO;
        newVote.choice.subject = vTxs[4];
        //voters[0].applyTxVote(newVote);
        voters[0].v[tip].txVotes[newVote.nRound][newVote.choice.subject].emplace(masternodeIds[i], newVote);

        // Round 2
        newVote.nRound = 2;
        newVote.choice.decision = CVoteChoice::Decision::PASS;
        newVote.choice.subject = vTxs[5];
        //voters[0].applyTxVote(newVote);
        voters[0].v[tip].txVotes[newVote.nRound][newVote.choice.subject].emplace(masternodeIds[i], newVote);
    }

    for (uint64_t i = 0; i < 22; i++) {
        // Round 1
        CTxVote newVote;
        newVote.nRound = 1;
        newVote.tip = tip;
        newVote.voter = masternodeIds[i];
        newVote.choice.decision = CVoteChoice::Decision::YES;
        newVote.choice.subject = vTxs[3];
        //voters[0].applyTxVote(newVote);
        voters[0].v[tip].txVotes[newVote.nRound][newVote.choice.subject].emplace(masternodeIds[i], newVote);

        CRoundVote newVoteR;
        newVoteR.nRound = 1;
        newVoteR.tip = tip;
        newVoteR.voter = masternodeIds[i];
        newVoteR.choice.decision = CVoteChoice::Decision::YES;
        newVoteR.choice.subject = vViceBlocks[1];
        //voters[0].applyRoundVote(newVoteR);
        voters[0].v[tip].roundVotes[newVoteR.nRound].emplace(masternodeIds[i], newVoteR);

        // Round 2
        newVoteR.nRound = 2;
        newVoteR.choice.decision = CVoteChoice::Decision::YES;
        newVoteR.choice.subject = vViceBlocks[3];
        //voters[0].applyRoundVote(newVoteR);
        voters[0].v[tip].roundVotes[newVoteR.nRound].emplace(masternodeIds[i], newVoteR);
    }

    for (uint64_t i = 22; i < 23; i++) {
        // Round 1
        CRoundVote newVoteR;
        newVoteR.nRound = 1;
        newVoteR.tip = tip;
        newVoteR.voter = masternodeIds[i];
        newVoteR.choice.decision = CVoteChoice::Decision::PASS;
        //voters[0].applyRoundVote(newVoteR);
        voters[0].v[tip].roundVotes[newVoteR.nRound].emplace(masternodeIds[i], newVoteR);

        // Round 2
        CTxVote newVote;
        newVote.nRound = 2;
        newVote.tip = tip;
        newVote.voter = masternodeIds[i];
        newVote.choice.decision = CVoteChoice::Decision::NO;
        newVote.choice.subject = vTxs[3];
        //voters[0].applyTxVote(newVote);
        voters[0].v[tip].txVotes[newVote.nRound][newVote.choice.subject].emplace(masternodeIds[i], newVote);
    }

    for (uint64_t i = 100; i < 200; i++) {
        // Round 2
        CRoundVote newVoteR;
        newVoteR.nRound = 2;
        newVoteR.tip = tip;
        newVoteR.voter = masternodeIds[i];
        newVoteR.choice.decision = CVoteChoice::Decision::YES;
        newVoteR.choice.subject = vViceBlocks[2];
        voters[0].v[tip].roundVotes[newVoteR.nRound].emplace(masternodeIds[i], newVoteR);
        //voters[0].applyRoundVote(newVoteR);

        CTxVote newVote;
        newVote.nRound = 2;
        newVote.tip = tip;
        newVote.voter = masternodeIds[i];
        newVote.choice.decision = CVoteChoice::Decision::YES;
        newVote.choice.subject = vTxs[0];
        //voters[0].applyTxVote(newVote);
        voters[0].v[tip].txVotes[newVote.nRound][newVote.choice.subject].emplace(masternodeIds[i], newVote);
        newVote.choice.decision = CVoteChoice::Decision::PASS;
        newVote.choice.subject = vTxs[1];
        //voters[0].applyTxVote(newVote);
        voters[0].v[tip].txVotes[newVote.nRound][newVote.choice.subject].emplace(masternodeIds[i], newVote);
        newVote.choice.decision = CVoteChoice::Decision::NO;
        newVote.choice.subject = vTxs[2];
        //voters[0].applyTxVote(newVote);
        voters[0].v[tip].txVotes[newVote.nRound][newVote.choice.subject].emplace(masternodeIds[i], newVote);
    }
}

}

TEST(dPoS_calls, TestStats)
{
    dpos::CDposVoter::Callbacks callbacks;
    callbacks.validateTx = [](const CTransaction&)
    {
        return true;
    };
    callbacks.validateTxs = [](const std::map<TxIdSorted, CTransaction>&)
    {
        return true;
    };
    callbacks.validateBlock = [](const CBlock& b, const std::map<TxIdSorted, CTransaction>& txs, bool checkTxs)
    {
        return true;
    };
    callbacks.allowArchiving = [](BlockHash votingId)
    {
        return true;
    };

    // Init voters
    std::vector<CMasternode::ID> masternodeIds;
    std::vector<dpos::CDposVoterTesting> voters;
    std::array<TxId, 6> vTxs;
    std::array<BlockHash, 4> vViceBlocks;
    BlockHash tip = uint256S("0xB101");
    dpos::initVoters(masternodeIds, voters, tip, callbacks, vTxs, vViceBlocks);

    auto stats1 = voters[0].priv_calcRoundVotingStats(1);
    auto stats2 = voters[0].priv_calcRoundVotingStats(2);

    auto stats0_1 = voters[0].priv_calcTxVotingStats(vTxs[0], 1);
    auto stats1_1 = voters[0].priv_calcTxVotingStats(vTxs[1], 1);
    auto stats2_1 = voters[0].priv_calcTxVotingStats(vTxs[2], 1);
    auto stats3_1 = voters[0].priv_calcTxVotingStats(vTxs[3], 1);
    auto stats4_1 = voters[0].priv_calcTxVotingStats(vTxs[4], 1);
    auto stats5_1 = voters[0].priv_calcTxVotingStats(vTxs[5], 1);

    auto stats0_2 = voters[0].priv_calcTxVotingStats(vTxs[0], 2);
    auto stats1_2 = voters[0].priv_calcTxVotingStats(vTxs[1], 2);
    auto stats2_2 = voters[0].priv_calcTxVotingStats(vTxs[2], 2);
    auto stats3_2 = voters[0].priv_calcTxVotingStats(vTxs[3], 2);
    auto stats4_2 = voters[0].priv_calcTxVotingStats(vTxs[4], 2);
    auto stats5_2 = voters[0].priv_calcTxVotingStats(vTxs[5], 2);

    auto stats0_3 = voters[0].priv_calcTxVotingStats(vTxs[0], 3);
    auto stats1_3 = voters[0].priv_calcTxVotingStats(vTxs[1], 3);
    auto stats2_3 = voters[0].priv_calcTxVotingStats(vTxs[2], 3);
    auto stats3_3 = voters[0].priv_calcTxVotingStats(vTxs[3], 3);
    auto stats4_3 = voters[0].priv_calcTxVotingStats(vTxs[4], 3);
    auto stats5_3 = voters[0].priv_calcTxVotingStats(vTxs[5], 3);

    ASSERT_EQ(stats1.totus(), 23);
    ASSERT_EQ(stats1.pro[vViceBlocks[0]], 0);
    ASSERT_EQ(stats1.pro[vViceBlocks[1]], 22);
    ASSERT_EQ(stats1.abstinendi, 1);

    ASSERT_EQ(stats2.totus(), 122);
    ASSERT_EQ(stats2.pro[vViceBlocks[2]], 100);
    ASSERT_EQ(stats2.pro[vViceBlocks[3]], 22);
    ASSERT_EQ(stats2.abstinendi, 0);

    { // txs round 1
        ASSERT_EQ(stats0_1.pro, 100);
        ASSERT_EQ(stats0_1.abstinendi, 0);
        ASSERT_EQ(stats0_1.contra, 0);

        ASSERT_EQ(stats1_1.totus(), 0);

        ASSERT_EQ(stats2_1.pro, 0);
        ASSERT_EQ(stats2_1.abstinendi, 0);
        ASSERT_EQ(stats2_1.contra, 100);

        ASSERT_EQ(stats3_1.totus(), 23);
        ASSERT_EQ(stats3_1.pro, 22);
        ASSERT_EQ(stats3_1.abstinendi, 0);
        ASSERT_EQ(stats3_1.contra, 1);

        ASSERT_EQ(stats4_1.pro, 0);
        ASSERT_EQ(stats4_1.abstinendi, 0);
        ASSERT_EQ(stats4_1.contra, 23);

        ASSERT_EQ(stats5_1.totus(), 0);
    }


    { // txs round 2
        ASSERT_EQ(stats0_2.pro, 100);
        ASSERT_EQ(stats0_2.abstinendi, 0);
        ASSERT_EQ(stats0_2.contra, 0);

        ASSERT_EQ(stats1_2.totus(), 100);
        ASSERT_EQ(stats1_2.abstinendi, 100);

        ASSERT_EQ(stats2_2.pro, 0);
        ASSERT_EQ(stats2_2.abstinendi, 0);
        ASSERT_EQ(stats2_2.contra, 100);

        ASSERT_EQ(stats3_2.totus(), 23);
        ASSERT_EQ(stats3_2.pro, 22);
        ASSERT_EQ(stats3_2.abstinendi, 0);
        ASSERT_EQ(stats3_2.contra, 1);

        ASSERT_EQ(stats4_2.pro, 0);
        ASSERT_EQ(stats4_2.abstinendi, 0);
        ASSERT_EQ(stats4_2.contra, 23);

        ASSERT_EQ(stats5_2.pro, 0);
        ASSERT_EQ(stats5_2.abstinendi, 23);
        ASSERT_EQ(stats5_2.contra, 0);
    }


    { // txs round 3
        ASSERT_EQ(stats0_3.pro, 100);
        ASSERT_EQ(stats0_3.abstinendi, 0);
        ASSERT_EQ(stats0_3.contra, 0);

        ASSERT_EQ(stats1_3.totus(), 0);

        ASSERT_EQ(stats2_3.pro, 0);
        ASSERT_EQ(stats2_3.abstinendi, 0);
        ASSERT_EQ(stats2_3.contra, 100);

        ASSERT_EQ(stats3_3.totus(), 23);
        ASSERT_EQ(stats3_3.pro, 22);
        ASSERT_EQ(stats3_3.abstinendi, 0);
        ASSERT_EQ(stats3_3.contra, 1);

        ASSERT_EQ(stats4_3.pro, 0);
        ASSERT_EQ(stats4_3.abstinendi, 0);
        ASSERT_EQ(stats4_3.contra, 23);

        ASSERT_EQ(stats5_3.totus(), 0);
    }

    // test txHasAnyVote, wasTxLost
    for (int i = 0; i < 6; i++) {
        ASSERT_TRUE(voters[0].priv_txHasAnyVote(vTxs[i]));
        ASSERT_FALSE(voters[0].priv_wasTxLost(vTxs[i]));

    }
    ASSERT_FALSE(voters[0].priv_txHasAnyVote(TxId{}));
    ASSERT_FALSE(voters[0].priv_wasTxLost(TxId{}));

    // test filterFinishedTxs
    std::map<TxId, CTransaction> txs_f;
    txs_f = {
        {vTxs[0], CTransaction{}},
        {vTxs[1], CTransaction{}},
        {TxId{}, CTransaction{}}
    };
    ASSERT_EQ(txs_f.size(), 3);
    voters[0].priv_filterFinishedTxs(txs_f, 2);
    ASSERT_EQ(txs_f.size(), 1);
    ASSERT_EQ(txs_f.begin()->first, TxId{});

    txs_f = {
        {vTxs[0], CTransaction{}},
        {vTxs[1], CTransaction{}},
        {TxId{}, CTransaction{}}
    };
    ASSERT_EQ(txs_f.size(), 3);
    voters[0].priv_filterFinishedTxs(txs_f, 0);
    ASSERT_EQ(txs_f.size(), 2);
}

TEST(dPoS_calls, TestRoundStalamate)
{
    dpos::CDposVoterTesting voter(dpos::CDposVoterTesting::Callbacks{});
    voter.minQuorum = 23;
    voter.numOfVoters = 32;

    dpos::CRoundVotingDistribution stats;
    ASSERT_FALSE(voter.priv_checkRoundStalemate(stats));

    stats.abstinendi = 9;
    ASSERT_FALSE(voter.priv_checkRoundStalemate(stats));

    stats.abstinendi = 10;
    ASSERT_TRUE(voter.priv_checkRoundStalemate(stats));

    stats.abstinendi = 10;
    stats.pro[BlockHash{}] = 22;
    ASSERT_TRUE(voter.priv_checkRoundStalemate(stats));

    stats.abstinendi = 9;
    stats.pro[BlockHash{}] = 22;
    ASSERT_FALSE(voter.priv_checkRoundStalemate(stats));

    stats.pro[BlockHash{}] = 22;
    stats.pro[uint256S("0xB101")] = 22;
    ASSERT_TRUE(voter.priv_checkRoundStalemate(stats));

    stats.pro[BlockHash{}] = 22;
    stats.pro[uint256S("0xB101")] = 23;
    ASSERT_FALSE(voter.priv_checkRoundStalemate(stats));

    stats.pro[BlockHash{}] = 22;
    stats.pro[uint256S("0xB101")] = 100;
    ASSERT_FALSE(voter.priv_checkRoundStalemate(stats));
}

TEST(dPoS_calls, TestNotCommitable)
{
    dpos::CDposVoterTesting voter(dpos::CDposVoterTesting::Callbacks{});
    voter.minQuorum = 23;
    voter.numOfVoters = 32;

    dpos::CTxVotingDistribution stats;
    ASSERT_FALSE(voter.priv_checkTxNotCommittable(stats));

    stats.abstinendi = 9;
    ASSERT_FALSE(voter.priv_checkTxNotCommittable(stats));

    stats.abstinendi = 10;
    ASSERT_TRUE(voter.priv_checkTxNotCommittable(stats));

    stats.abstinendi = 10;
    stats.pro = 22;
    ASSERT_TRUE(voter.priv_checkTxNotCommittable(stats));

    stats.abstinendi = 9;
    stats.pro = 22;
    ASSERT_FALSE(voter.priv_checkTxNotCommittable(stats));

    stats.contra = 22;
    stats.pro = 22;
    ASSERT_TRUE(voter.priv_checkTxNotCommittable(stats));

    stats.contra = 22;
    stats.pro = 23;
    ASSERT_FALSE(voter.priv_checkTxNotCommittable(stats));

    stats.contra = 22;
    stats.pro = 100;
    ASSERT_FALSE(voter.priv_checkTxNotCommittable(stats));

    stats.contra = 100;
    stats.pro = 100;
    ASSERT_FALSE(voter.priv_checkTxNotCommittable(stats));
}
