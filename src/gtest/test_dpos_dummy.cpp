#include <gtest/gtest.h>

#include "../masternodes/dpos_voter.h"

namespace
{

void initVoters_dummy(std::array<CMasternode::ID, 32>& masternodeIds, std::array<dpos::CDposVoter, 32>& voters, BlockHash& tip)
{
    for (uint64_t i = 0; i < 32; i++) {
        masternodeIds[i] = ArithToUint256(arith_uint256{i});
    }

    dpos::CDposVoter::Callbacks callbacks;

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

    tip = uint256S("0xB101");
    for (uint64_t i = 0; i < 32; i++) {
        voters[i].minQuorum = 23;
        voters[i].numOfVoters = 32;
        voters[i].setVoting(tip, callbacks, true, masternodeIds[i]);
    }
}

}

TEST(dPoS, DummyEmptyBlock)
{
    // Init voters
    std::array<CMasternode::ID, 32> masternodeIds;
    std::array<dpos::CDposVoter, 32> voters;
    BlockHash tip;
    initVoters_dummy(masternodeIds, voters, tip);

    // Create dummy vice-block
    CBlock viceBlock;
    viceBlock.hashPrevBlock = tip;
    viceBlock.nRoundNumber = 1;

    dpos::CDposVoter::Output res;
    for (uint64_t i = 0; i < 23; i++) {
        { // wrong round check
            viceBlock.nRoundNumber = static_cast<dpos::Round>(i * 2);
            const size_t initSize = voters[i].v[tip].viceBlocks.size();
            const auto empty = voters[i].applyViceBlock(viceBlock);
            ASSERT_TRUE(empty.vTxVotes.empty());
            ASSERT_TRUE(!empty.blockToSubmit);
            ASSERT_TRUE(empty.vRoundVotes.empty());
            ASSERT_TRUE(empty.vErrors.empty());
            ASSERT_EQ(voters[i].v[tip].viceBlocks[viceBlock.GetHash()].GetHash(), viceBlock.GetHash());
            viceBlock.nRoundNumber = 1;
        }

        res += voters[i].applyViceBlock(viceBlock);

        ASSERT_EQ(voters[i].v.size(), 1);
        ASSERT_TRUE(res.vTxVotes.empty());
        ASSERT_TRUE(!res.blockToSubmit);
        ASSERT_TRUE(res.vErrors.empty());
        ASSERT_EQ(voters[i].v[tip].viceBlocks[viceBlock.GetHash()].GetHash(), viceBlock.GetHash());

        dpos::CRoundVote voteWant;
        voteWant.voter = masternodeIds[i];
        voteWant.nRound = 1;
        voteWant.tip = tip;
        voteWant.choice = dpos::CVoteChoice{viceBlock.GetHash(), dpos::CVoteChoice::Decision::YES};

        ASSERT_EQ(res.vRoundVotes.size(), i + 1);
        ASSERT_EQ(res.vRoundVotes[i], voteWant);

        const auto voter0out = voters[0].applyRoundVote(res.vRoundVotes[i]);
        if (i == 23 - 1) {
            // final vote
            ASSERT_TRUE(voter0out.vTxVotes.empty());
            ASSERT_TRUE(voter0out.vRoundVotes.empty());
            ASSERT_TRUE(voter0out.vErrors.empty());
            ASSERT_TRUE(voter0out.blockToSubmit);
            ASSERT_EQ(voter0out.blockToSubmit->block.GetHash(), viceBlock.GetHash());
            ASSERT_EQ(voter0out.blockToSubmit->vApprovedBy.size(), 23);
        }
        else {
            // not final
            ASSERT_TRUE(voter0out.vTxVotes.empty());
            ASSERT_TRUE(voter0out.vRoundVotes.empty());
            ASSERT_TRUE(voter0out.vErrors.empty());
            ASSERT_FALSE(voter0out.blockToSubmit);
        }

        { // duplicate check
            const auto empty = voters[i].applyViceBlock(viceBlock);
            ASSERT_TRUE(empty.empty());
        }

        { // duplicate check
            const auto empty = voters[i].doRoundVoting();
            ASSERT_TRUE(empty.empty());
        }

        { // duplicate check
            const auto empty = voters[i].doTxsVoting();
            ASSERT_TRUE(empty.empty());
        }
    }
}

TEST(dPoS, DummyCommitTx)
{
    // Init voters
    std::array<CMasternode::ID, 32> masternodeIds;
    std::array<dpos::CDposVoter, 32> voters;
    BlockHash tip;
    initVoters_dummy(masternodeIds, voters, tip);

    // create dummy tx
    CMutableTransaction mtx;
    mtx.fInstant = true;
    mtx.fOverwintered = true;
    mtx.nVersion = 4;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;
    CTransaction tx{mtx};

    dpos::CDposVoter::Output res;
    for (uint64_t i = 0; i < 23; i++) {
        res += voters[i].applyTx(tx);

        ASSERT_EQ(voters[i].v.size(), 1);
        ASSERT_TRUE(res.vRoundVotes.empty());
        ASSERT_TRUE(!res.blockToSubmit);
        ASSERT_TRUE(res.vErrors.empty());
        ASSERT_EQ(voters[i].v[tip].txs[tx.GetHash()].GetHash(), tx.GetHash());

        dpos::CTxVote voteWant;
        voteWant.voter = masternodeIds[i];
        voteWant.nRound = 1;
        voteWant.tip = tip;
        voteWant.choice = dpos::CVoteChoice{tx.GetHash(), dpos::CVoteChoice::Decision::YES};

        ASSERT_EQ(res.vTxVotes.size(), i + 1);
        ASSERT_EQ(res.vTxVotes[i], voteWant);

        const auto voter0out = voters[0].applyRoundVote(res.vTxVotes[i]);
        ASSERT_TRUE(voter0out.empty());
        if (i == 23 - 1) {
            // final vote
            ASSERT_EQ(voters[0].listCommittedTxs().size(), 1);
            ASSERT_EQ(voters[0].listCommittedTxs()[UintToArith256(tx.GetHash())].GetHash(), tx.GetHash());
        }

        { // duplicate check
            const auto empty = voters[i].applyTx(tx);
            ASSERT_TRUE(empty.empty());
        }

        { // duplicate check
            const auto empty = voters[i].doTxsVoting();
            ASSERT_TRUE(empty.empty());
        }
    }
}

TEST(dPoS, DummyRejectTx)
{
    // Init voters
    std::array<CMasternode::ID, 32> masternodeIds;
    std::array<dpos::CDposVoter, 32> voters;
    BlockHash tip;
    initVoters_dummy(masternodeIds, voters, tip);

    dpos::CDposVoter::Callbacks callbacks{};
    callbacks.validateTxs = [](const std::map<TxIdSorted, CTransaction>&)
    {
        return false;
    };
    callbacks.validateBlock = [](const CBlock& b, const std::map<TxIdSorted, CTransaction>& txs, bool checkTxs)
    {
        return true;
    };
    callbacks.allowArchiving = [](BlockHash votingId)
    {
        return true;
    };
    voters[0].setVoting(tip, callbacks, true, masternodeIds[0]);

    // create dummy tx
    CMutableTransaction mtx;
    mtx.fInstant = true;
    mtx.fOverwintered = true;
    mtx.nVersion = 4;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;
    CTransaction tx{mtx};

    dpos::CDposVoter::Output res;
    for (uint64_t i = 0; i < 1; i++) {
        res += voters[i].applyTx(tx);

        ASSERT_EQ(voters[i].v.size(), 0);
        ASSERT_TRUE(res.empty());
        ASSERT_TRUE(voters[i].v[tip].txs.empty());
    }
}
