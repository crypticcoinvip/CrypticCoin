// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "dpos_voter.h"
//struct VotingState
//    {
//        std::map<Round, std::map<TxId, std::map<CMasternode::ID, CDposVote> > > txVotes;
//        std::map<Round, std::map<CMasternode::ID, CDposVote> > roundVotes;

//        std::map<TxId, CTransaction> txs;
//        std::map<BlockHash, CBlock> viceBlocks;
//    };
//    struct Callbacks
//    {
//        ValidateTxsF validateTxs;
//        ValidateBlockF validateBlock;
//    };

//    CDposVoter(std::map<BlockHash, VotingState>* state, size_t minQuorum, size_t numOfVoters)
//        : v(*state)
//    {
//        this->minQuorum = minQuorum;
//        this->numOfVoters = numOfVoters;
//    }

//    void setVoting(BlockHash tip,
//                   Callbacks world,
//                   bool amIvoter,
//                   CMasternode::ID me)
//    {
//        this->tip = tip;
//        this->world = std::move(world);
//        this->amIvoter = amIvoter;
//        this->me = me;
//    }
