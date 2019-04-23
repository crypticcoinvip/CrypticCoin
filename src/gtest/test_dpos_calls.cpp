#include <gtest/gtest.h>
#include <chainparamsbase.h>

#include "../masternodes/dpos_voter.h"

namespace dpos {


TEST(dPoS_calls, TestTxCommitting) {
    std::vector<CBlock> blocks(4);
    blocks[0].nRound = 1;
    blocks[1].nRound = 2;
    blocks[2].nRound = 3;
    blocks[3].nRound = 4;

    dpos::CDposVoter::Callbacks callbacks;
    callbacks.getPrevBlock = [&](const BlockHash &block) {
        if (block == blocks[3].GetHash())
            return blocks[2].GetHash();
        if (block == blocks[2].GetHash())
            return blocks[1].GetHash();
        if (block == blocks[1].GetHash())
            return blocks[0].GetHash();
        return BlockHash{};
    };

    for (int i = 0; i < blocks.size(); i++) {
        dpos::CDposVoter voter(callbacks);
        voter.minQuorum = 23;
        voter.numOfVoters = 32;
        voter.maxNotVotedTxsToKeep = 500;
        voter.maxTxVotesFromVoter = 500;

        CMutableTransaction testTxCommitted_m;
        testTxCommitted_m.vin.resize(2);
        testTxCommitted_m.vin[1].prevout.hash = TxId{};
        testTxCommitted_m.vin[1].prevout.n = 0;
        testTxCommitted_m.vin[0].prevout.hash = TxId{};
        testTxCommitted_m.vin[0].prevout.n = 1;

        CMutableTransaction testTxUncommitable_m;
        testTxUncommitable_m.vin.resize(2);
        testTxUncommitable_m.vin[1].prevout.hash = TxId{};
        testTxUncommitable_m.vin[1].prevout.n = 2;
        testTxUncommitable_m.vin[0].prevout.hash = TxId{};
        testTxUncommitable_m.vin[0].prevout.n = 1;

        voter.txs.emplace(testTxCommitted_m.GetHash(), CTransaction{testTxCommitted_m});
        voter.pledgedInputs.emplace(testTxCommitted_m.vin[0].prevout, testTxCommitted_m.GetHash());
        voter.pledgedInputs.emplace(testTxCommitted_m.vin[1].prevout, testTxCommitted_m.GetHash());

        voter.txs.emplace(testTxUncommitable_m.GetHash(), testTxUncommitable_m);

        for (uint64_t mi = 0; mi < 23; mi++) {
            CMasternode::ID mId = ArithToUint256(arith_uint256{mi});

            CTxVote newVote;
            newVote.nRound = 1;
            newVote.tip = blocks[i].GetHash();
            newVote.voter = mId;
            newVote.choice.decision = CVoteChoice::Decision::YES;
            newVote.choice.subject = testTxCommitted_m.GetHash();
            //voters[0].applyTxVote(newVote);
            voter.v[blocks[i].GetHash()].txVotes[newVote.choice.subject].emplace(mId, newVote);
            voter.v[blocks[i].GetHash()].mnTxVotes[mId].push_back(newVote);

            // some random tx, it isn't uncommittable
            ASSERT_FALSE(voter.checkTxNotCommittable(TxId{}, blocks[i].GetHash()));
            // some random tx, it isn't committed
            ASSERT_FALSE(voter.isCommittedTx(TxId{}, blocks[i].GetHash(), 1));

            if (mi < 23 - 1) {
                ASSERT_FALSE(voter.checkTxNotCommittable(testTxUncommitable_m.GetHash(), blocks[i].GetHash()));
                ASSERT_FALSE(voter.isCommittedTx(testTxCommitted_m.GetHash(), blocks[i].GetHash(), 1));
            } else { // committed
                for (int j = 0; j < blocks.size(); j++) {
                    if (i == j) //(j <= i && j > i - CDposVoter::GUARANTEES_MEMORY)
                        ASSERT_TRUE(
                                voter.checkTxNotCommittable(testTxUncommitable_m.GetHash(), blocks[j].GetHash()));
                    else
                        ASSERT_FALSE(
                                voter.checkTxNotCommittable(testTxUncommitable_m.GetHash(), blocks[j].GetHash()));
                }
                ASSERT_TRUE(voter.isCommittedTx(testTxCommitted_m.GetHash(), blocks[i].GetHash(), 1));
            }

            ASSERT_FALSE(voter.isCommittedTx(testTxUncommitable_m.GetHash(), blocks[i].GetHash(), 1));
            ASSERT_FALSE(voter.checkTxNotCommittable(testTxCommitted_m.GetHash(), blocks[i].GetHash()));
        }
    }
}

}