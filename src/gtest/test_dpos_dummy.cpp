#include <gtest/gtest.h>

#include "../masternodes/dpos_voter.h"

TEST(dPoS, NoTransactionsMeansEmptyBlock)
{
    std::vector<CMasternode::ID> masternodeIds;
    for (uint64_t i = 0; i < 32; i++) {
        masternodeIds.push_back(ArithToUint256(arith_uint256{i}));
    }

    dpos::CDposVoter::Callbacks callbacks;

    callbacks.validateTxs = [&](const std::map<TxIdSorted, CTransaction>&) {
        return true;
    };
    callbacks.validateBlock = [&](const CBlock& b, const std::map<TxIdSorted, CTransaction>& txs, bool checkTxs) {
        return true;
    };
    callbacks.allowArchiving = [&](BlockHash votingId) {
        return true;
    };

    std::array<dpos::CDposVoter, 32> voters;
    for (uint64_t i = 0; i < 32; i++) {
        voters[i].minQuorum = 23;
        voters[i].numOfVoters = 32;
        voters[i].setVoting(uint256S("0xB101"), callbacks, true, masternodeIds[i]);
    }

    CBlock viceBlock;
    viceBlock.hashPrevBlock = uint256S("0xB101");
    viceBlock.nRoundNumber = 1;
    dpos::CDposVoter::Output res;
    for (uint64_t i = 0; i < 23; i++) {
        { // wrong round check
            viceBlock.nRoundNumber = static_cast<dpos::Round>(i * 2);
            const size_t initSize = voters[i].v[viceBlock.hashPrevBlock].viceBlocks.size();
            const auto empty = voters[i].applyViceBlock(viceBlock);
            ASSERT_TRUE(empty.vTxVotes.empty());
            ASSERT_TRUE(!empty.blockToSubmit);
            ASSERT_TRUE(empty.vRoundVotes.empty());
            ASSERT_TRUE(empty.vErrors.empty());
            ASSERT_EQ(voters[i].v[viceBlock.hashPrevBlock].viceBlocks[viceBlock.GetHash()].GetHash(), viceBlock.GetHash());
            viceBlock.nRoundNumber = 1;
        }

        res += voters[i].applyViceBlock(viceBlock);

        ASSERT_TRUE(res.vTxVotes.empty());
        ASSERT_TRUE(!res.blockToSubmit);
        ASSERT_TRUE(res.vErrors.empty());
        ASSERT_EQ(voters[i].v[viceBlock.hashPrevBlock].viceBlocks[viceBlock.GetHash()].GetHash(), viceBlock.GetHash());

        dpos::CTxVote voteWant;
        voteWant.voter = masternodeIds[i];
        voteWant.nRound = 1;
        voteWant.tip = viceBlock.hashPrevBlock;
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
        } else {
            // not final
            ASSERT_TRUE(voter0out.vTxVotes.empty());
            ASSERT_TRUE(voter0out.vRoundVotes.empty());
            ASSERT_TRUE(voter0out.vErrors.empty());
            ASSERT_FALSE(voter0out.blockToSubmit);
        }

        { // duplicate check
            const auto empty = voters[i].applyViceBlock(viceBlock);
            ASSERT_TRUE(empty.vTxVotes.empty());
            ASSERT_TRUE(!empty.blockToSubmit);
            ASSERT_TRUE(empty.vRoundVotes.empty());
            ASSERT_TRUE(empty.vErrors.empty());
        }

        {
            const auto empty = voters[i].doRoundVoting();
            ASSERT_TRUE(empty.vTxVotes.empty());
            ASSERT_TRUE(!empty.blockToSubmit);
            ASSERT_TRUE(empty.vRoundVotes.empty());
            ASSERT_TRUE(empty.vErrors.empty());
        }

        {
            const auto empty = voters[i].doTxsVoting();
            ASSERT_TRUE(empty.vTxVotes.empty());
            ASSERT_TRUE(!empty.blockToSubmit);
            ASSERT_TRUE(empty.vRoundVotes.empty());
            ASSERT_TRUE(empty.vErrors.empty());
        }
    }
}