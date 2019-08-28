#include <gtest/gtest.h>
#include <chainparamsbase.h>

#include "../masternodes/dpos_voter.h"

namespace dpos {


TEST(dPoS_calls, TestTxCommitting) {
    std::vector<CBlock> blocks(4);
    blocks[0].nTime = 1;
    blocks[1].nTime = 2;
    blocks[2].nTime = 3;
    blocks[3].nTime = 4;

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

        voter.updateTip(blocks[i].GetHash());

        CMutableTransaction txApproved_m;
        txApproved_m.vin.resize(2);
        txApproved_m.vin[1].prevout.hash = TxId{};
        txApproved_m.vin[1].prevout.n = 0;
        txApproved_m.vin[0].prevout.hash = TxId{};
        txApproved_m.vin[0].prevout.n = 1;

        CMutableTransaction txRejected_m;
        txRejected_m.vin.resize(2);
        txRejected_m.vin[1].prevout.hash = TxId{};
        txRejected_m.vin[1].prevout.n = 2;
        txRejected_m.vin[0].prevout.hash = TxId{};
        txRejected_m.vin[0].prevout.n = 1;

        voter.txs.emplace(txApproved_m.GetHash(), CTransaction{txApproved_m});
        voter.pledgedInputs.emplace(txApproved_m.vin[0].prevout, txApproved_m.GetHash());
        voter.pledgedInputs.emplace(txApproved_m.vin[1].prevout, txApproved_m.GetHash());

        voter.txs.emplace(txRejected_m.GetHash(), txRejected_m);

        for (uint64_t mi = 0; mi < 23; mi++) {
            CMasternode::ID mId = ArithToUint256(arith_uint256{mi});

            CTxVote newVote;
            newVote.nRound = 1;
            newVote.tip = blocks[i].GetHash();
            newVote.voter = mId;
            newVote.choice.decision = CVoteChoice::Decision::YES;
            newVote.choice.subject = txApproved_m.GetHash();

            voter.insertTxVote(newVote);

            // some random tx, it isn't uncommittable
            ASSERT_FALSE(voter.isNotCommittableTx(TxId{}));
            // some random tx, it isn't committed
            ASSERT_FALSE(voter.isCommittedTx(TxId{}, blocks[i].GetHash(), 1));

            if (mi < 23 - 1) {
                ASSERT_FALSE(voter.isNotCommittableTx(txRejected_m.GetHash()));
                ASSERT_FALSE(voter.isCommittedTx(txApproved_m.GetHash(), blocks[i].GetHash()));
            } else { // committed
                ASSERT_TRUE(voter.isCommittedTx(txApproved_m.GetHash(), blocks[i].GetHash()));
                ASSERT_TRUE(voter.isCommittedTx(txApproved_m.GetHash(), blocks[i].GetHash(), 0, 1));
                ASSERT_TRUE(voter.isNotCommittableTx(txRejected_m.GetHash()));
            }

            ASSERT_FALSE(voter.isCommittedTx(txApproved_m.GetHash(), blocks[i].GetHash(), 1, 1));

            ASSERT_FALSE(voter.isCommittedTx(txRejected_m.GetHash(), blocks[i].GetHash()));
            ASSERT_FALSE(voter.isCommittedTx(txRejected_m.GetHash(), blocks[i].GetHash(), 0, 1));

            ASSERT_FALSE(voter.isNotCommittableTx(txApproved_m.GetHash()));
        }
    }
}

}