#include <gtest/gtest.h>

#include "../masternodes/dpos_voter.h"

namespace
{

void initVoters_dummy(std::vector<CMasternode::ID>& masternodeIds,
                      std::vector<dpos::CDposVoter>& voters,
                      BlockHash tip,
                      dpos::CDposVoter::Callbacks callbacks)
{
    for (uint64_t i = 0; i < 32; i++) {
        masternodeIds.emplace_back(ArithToUint256(arith_uint256{i}));
        voters.emplace_back(callbacks);
    }

    for (uint64_t i = 0; i < 32; i++) {
        voters[i].minQuorum = 23;
        voters[i].numOfVoters = 32;
        voters[i].maxNotVotedTxsToKeep = 100;
        voters[i].maxTxVotesFromVoter = 100;
        voters[i].offlineVoters = 0;
        voters[i].updateTip(tip);
        voters[i].setVoting(true, masternodeIds[i]);
    }
}

}

TEST(dPoS, DummyEmptyBlock)
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
    std::vector<dpos::CDposVoter> voters;
    BlockHash tip = uint256S("0xB101");
    initVoters_dummy(masternodeIds, voters, tip, callbacks);

    // Create dummy vice-block
    CBlock viceBlock;
    viceBlock.hashPrevBlock = tip;
    viceBlock.nRound = 1;

    dpos::CDposVoter::Output res;
    for (uint64_t i = 0; i < 23; i++) {
        { // future round check
            viceBlock.nRound = static_cast<dpos::Round>(i + 2);
            const auto empty = voters[i].applyViceBlock(viceBlock);
            ASSERT_TRUE(empty.vTxVotes.empty());
            ASSERT_TRUE(!empty.blockToSubmit);
            ASSERT_TRUE(empty.vRoundVotes.empty());
            ASSERT_TRUE(empty.vErrors.empty());
            ASSERT_EQ(voters[i].v[tip].viceBlocks[viceBlock.GetHash()].GetHash(), viceBlock.GetHash());
            viceBlock.nRound = 1;
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
    std::vector<dpos::CDposVoter> voters;
    BlockHash tip = uint256S("0xB101");
    initVoters_dummy(masternodeIds, voters, tip, callbacks);

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
        ASSERT_EQ(voters[i].txs[tx.GetHash()].GetHash(), tx.GetHash());

        dpos::CTxVote voteWant;
        voteWant.voter = masternodeIds[i];
        voteWant.nRound = 1;
        voteWant.tip = tip;
        voteWant.choice = dpos::CVoteChoice{tx.GetHash(), dpos::CVoteChoice::Decision::YES};

        ASSERT_EQ(res.vTxVotes.size(), i + 1);
        ASSERT_EQ(res.vTxVotes[i], voteWant);

        const auto voter0out = voters[0].applyTxVote(res.vTxVotes[i]);
        ASSERT_TRUE(voter0out.empty());
        if (i == 23 - 1) {
            // final vote
            ASSERT_EQ(voters[0].listCommittedTxs().size(), 1);
            ASSERT_EQ(voters[0].listCommittedTxs()[tx.GetDposSortingHash()].GetHash(), tx.GetHash());
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
    dpos::CDposVoter::Callbacks callbacks{};
    callbacks.validateTx = [](const CTransaction&)
    {
        return false;
    };
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

    // Init voters
    std::vector<CMasternode::ID> masternodeIds;
    std::vector<dpos::CDposVoter> voters;
    BlockHash tip = uint256S("0xB101");
    initVoters_dummy(masternodeIds, voters, tip, callbacks);

    voters[0].setVoting(true, masternodeIds[0]);

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
        ASSERT_TRUE(res.vTxReqs.empty());
        ASSERT_TRUE(res.vRoundVotes.empty());
        ASSERT_TRUE(!res.blockToSubmit);
        ASSERT_FALSE(res.vErrors.empty()); // err
        ASSERT_TRUE(voters[i].txs.empty());
        ASSERT_TRUE(voters[i].txs.empty());
    }
}

TEST(dPoS, InvalidVote)
{
    dpos::CDposVoter::Callbacks callbacks{};
    callbacks.validateTx = [](const CTransaction&)
    {
        return false;
    };
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

    // Init voters
    std::vector<CMasternode::ID> masternodeIds;
    std::vector<dpos::CDposVoter> voters;
    BlockHash tip = uint256S("0xB101");
    initVoters_dummy(masternodeIds, voters, tip, callbacks);

    voters[0].setVoting(true, masternodeIds[0]);

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
        // try to apply vote with nRound = 0
        dpos::CTxVote voteInvalid;
        voteInvalid.voter = masternodeIds[i];
        voteInvalid.nRound = 0;
        voteInvalid.tip = tip;
        voteInvalid.choice = dpos::CVoteChoice{tx.GetHash(), dpos::CVoteChoice::Decision::YES};
        res = voters[i].applyTxVote(voteInvalid);
        ASSERT_EQ(voters[i].v.size(), 0);
        ASSERT_TRUE(res.vRoundVotes.empty());
        ASSERT_TRUE(!res.blockToSubmit);
        ASSERT_FALSE(res.vErrors.empty()); // err
        ASSERT_TRUE(voters[i].txs.empty());


        dpos::CRoundVote voteRInvalid;
        voteRInvalid.voter = masternodeIds[i];
        voteRInvalid.nRound = 0;
        voteRInvalid.tip = tip;
        voteRInvalid.choice = dpos::CVoteChoice{uint256S("0xB101"), dpos::CVoteChoice::Decision::YES};
        res = voters[i].applyRoundVote(voteRInvalid);
        ASSERT_EQ(voters[i].v.size(), 0);
        ASSERT_TRUE(res.vRoundVotes.empty());
        ASSERT_TRUE(!res.blockToSubmit);
        ASSERT_FALSE(res.vErrors.empty()); // err
        ASSERT_TRUE(voters[i].txs.empty());

        // try to apply vote with invalid decision
        voteInvalid.nRound = 1;
        voteInvalid.choice = dpos::CVoteChoice{tx.GetHash(), 55};
        res = voters[i].applyTxVote(voteInvalid);
        ASSERT_EQ(voters[i].v.size(), 0);
        ASSERT_TRUE(res.vRoundVotes.empty());
        ASSERT_TRUE(!res.blockToSubmit);
        ASSERT_FALSE(res.vErrors.empty()); // err
        ASSERT_TRUE(voters[i].txs.empty());


        voteRInvalid.nRound = 1;
        voteRInvalid.choice = dpos::CVoteChoice{uint256S("0xB101"), 55};
        res = voters[i].applyRoundVote(voteRInvalid);
        ASSERT_EQ(voters[i].v.size(), 0);
        ASSERT_TRUE(res.vRoundVotes.empty());
        ASSERT_TRUE(!res.blockToSubmit);
        ASSERT_FALSE(res.vErrors.empty()); // err
        ASSERT_TRUE(voters[i].txs.empty());


        // try to apply round vote with NO
        voteRInvalid.choice = dpos::CVoteChoice{uint256S("0xB101"), dpos::CVoteChoice::Decision::NO};
        res = voters[i].applyRoundVote(voteRInvalid);
        ASSERT_EQ(voters[i].v.size(), 0);
        ASSERT_TRUE(res.vRoundVotes.empty());
        ASSERT_TRUE(!res.blockToSubmit);
        ASSERT_FALSE(res.vErrors.empty()); // err
        ASSERT_TRUE(voters[i].txs.empty());


        // try to apply round vote with PASS, but not empty hash
        voteRInvalid.choice = dpos::CVoteChoice{uint256S("0xB101"), dpos::CVoteChoice::Decision::NO};
        res = voters[i].applyRoundVote(voteRInvalid);
        ASSERT_EQ(voters[i].v.size(), 0);
        ASSERT_TRUE(res.vRoundVotes.empty());
        ASSERT_TRUE(!res.blockToSubmit);
        ASSERT_FALSE(res.vErrors.empty()); // err
        ASSERT_TRUE(voters[i].txs.empty());


        // Try to doublesign with round vote
        voteRInvalid.choice = dpos::CVoteChoice{uint256S("0xB101"), dpos::CVoteChoice::Decision::YES};
        res = voters[i].applyRoundVote(voteRInvalid);
        ASSERT_EQ(voters[i].v.size(), 1);
        ASSERT_TRUE(res.vErrors.empty());

        voteRInvalid.choice = dpos::CVoteChoice{BlockHash{},
                                                dpos::CVoteChoice::Decision::PASS}; // pass after voted for another vice block
        res = voters[i].applyRoundVote(voteRInvalid);
        ASSERT_FALSE(res.vErrors.empty()); // err

        voteRInvalid.choice =
            dpos::CVoteChoice{uint256S("0xB102"), dpos::CVoteChoice::Decision::YES}; // vote for another vice block
        res = voters[i].applyRoundVote(voteRInvalid);
        ASSERT_FALSE(res.vErrors.empty()); // err


        // Try to doublesign with tx vote
        voteInvalid.choice = dpos::CVoteChoice{tx.GetHash(), dpos::CVoteChoice::Decision::YES};
        res = voters[i].applyTxVote(voteInvalid);
        ASSERT_TRUE(res.vErrors.empty());

        voteInvalid.choice = dpos::CVoteChoice{tx.GetHash(), dpos::CVoteChoice::Decision::PASS};
        res = voters[i].applyTxVote(voteInvalid);
        ASSERT_FALSE(res.vErrors.empty()); // err
    }
}
