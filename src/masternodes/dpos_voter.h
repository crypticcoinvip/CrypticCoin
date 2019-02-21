// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef DPOS_VOTER_H
#define DPOS_VOTER_H

#include "../uint256.h"
#include "../arith_uint256.h"
#include "../serialize.h"
#include "../primitives/block.h"
#include "dpos_p2p_messages.h"
#include "masternodes.h"
#include <map>
#include <mutex>
#include <algorithm>
#include <functional>
#include <numeric>
#include <utility>
#include <boost/range/numeric.hpp>
#include <boost/range/adaptor/map.hpp>


namespace dpos
{

using Round = uint16_t;
using TxId = uint256;
using TxIdSorted = arith_uint256;
using BlockHash = uint256;

using LockGuard = std::lock_guard<std::mutex>;

struct CDposVote
{
    CMasternode::ID voter;
    Round nRound = 0;
    BlockHash tip;
    CVoteChoice choice;
};

bool operator==(const CDposVote& l, const CDposVote& r)
{
    if (l.voter != r.voter)
        return false;
    if (l.nRound != r.nRound)
        return false;
    if (l.tip != r.tip)
        return false;
    if (l.choice.subject != r.choice.subject)
        return false;
    if (l.choice.decision != r.choice.decision)
        return false;
    return true;
}

bool operator!=(const CDposVote& l, const CDposVote& r)
{
    return !(l == r);
}

using CTxVote = CDposVote;
using CRoundVote = CDposVote;

struct TxVotingDistribution
{
    size_t pro;
    size_t contra;
    size_t abstinendi;
    size_t totus() const
    {
        return pro + contra + abstinendi;
    }
};

struct RoundVotingDistribution
{
    std::map<BlockHash, size_t> pro;
    size_t abstinendi;
    size_t totus() const
    {
        return boost::accumulate(pro | boost::adaptors::map_values, 0) + abstinendi;
    }
};

struct CDposVoterOutput
{
    std::vector<CDposVote> vTxVotes;
    std::vector<CDposVote> vRoundVotes;

    boost::optional<CBlock> blockToSubmit;

    bool invalidState = false;
};

CDposVoterOutput operator+(const CDposVoterOutput& l, const CDposVoterOutput& r)
{
    CDposVoterOutput res = l;
    res.vTxVotes.reserve(res.vTxVotes.size() + r.vTxVotes.size());
    res.vRoundVotes.reserve(res.vRoundVotes.size() + r.vRoundVotes.size());

    std::copy(r.vTxVotes.begin(), r.vTxVotes.end(), res.vTxVotes.end());
    std::copy(r.vRoundVotes.begin(), r.vRoundVotes.end(), res.vRoundVotes.end());
    if (r.blockToSubmit)
        res.blockToSubmit = r.blockToSubmit;
    res.invalidState |= r.invalidState;
    return res;
}

CDposVoterOutput operator+=(const CDposVoterOutput& l, const CDposVoterOutput& r)
{
    return l + r;
}

class CDposVoter
{
public:
    using ValidateTxsF = std::function<bool(const std::map<TxIdSorted, CTransaction>&)>;
    using ValidateBlockF = std::function<bool(const CBlock&, const std::map<TxIdSorted, CTransaction>&, bool)>;
    using Output = CDposVoterOutput;

    struct VotingState
    {
        std::map<Round, std::map<TxId, std::map<CMasternode::ID, CDposVote> > > txVotes;
        std::map<Round, std::map<CMasternode::ID, CDposVote> > roundVotes;

        std::map<TxId, CTransaction> txs;
        std::map<BlockHash, CBlock> viceBlocks;
    };
    struct Callbacks
    {
        ValidateTxsF validateTxs;
        ValidateBlockF validateBlock;
    };

    CDposVoter(std::map<BlockHash, VotingState>* state, size_t minQuorum, size_t numOfVoters)
        : v(*state)
    {
        this->minQuorum = minQuorum;
        this->numOfVoters = numOfVoters;
    }

    void setVoting(BlockHash tip,
                   Callbacks world,
                   bool amIvoter,
                   CMasternode::ID me)
    {
        this->tip = tip;
        this->world = std::move(world);
        this->amIvoter = amIvoter;
        this->me = me;
    }

    Output applyViceBlock(const CBlock& viceBlock)
    {
        if (!world.validateBlock(viceBlock, {}, false))
            return misbehavingErr();

        if (viceBlock.hashPrevBlock != tip) {
            return {};
        }

        if (!v[tip].viceBlocks.emplace(viceBlock.GetHash(), viceBlock).second) {
            LogPrintf("%s: Ignoring duplicating vice-block: %s \n", __func__, viceBlock.GetHash().GetHex());
            return {};
        }

        LogPrintf("%s: Received vice-block %s \n", __func__, viceBlock.GetHash().GetHex());
        return doRoundVoting();
    }

    Output applyTx(const CTransaction& tx)
    {
        assert(tx.fInstant);

        TxId txid = tx.GetHash();
        std::map<TxIdSorted, CTransaction> tx_m{{UintToArith256(txid), tx}};

        if (!world.validateTxs(tx_m)) {
            LogPrintf("%s: Received invalid tx %s \n", __func__, txid.GetHex());
            return {};
        }

        bool wasLost = wasTxLost(txid);
        v[tip].txs[txid] = tx;

        Output out{};
        if (wasLost) {
            out += doTxsVoting();
            out += doRoundVoting();
        }
        else {
            out += voteForTx(tx);
        }
        return out;
    }

    Output applyTxVote(const CTxVote& vote)
    {
        LogPrintf("%s: Received transaction vote for %s, from %s \n",
                  __func__,
                  vote.choice.subject.GetHex(),
                  vote.voter.GetHex());
        auto& txVoting = v[tip].txVotes[vote.nRound][vote.choice.subject];

        // Check misbehaving or duplicating
        if (txVoting.count(vote.voter) > 0) {
            if (txVoting[vote.voter] != vote) {
                LogPrintf("%s: MISBEHAVING MASTERNODE! doublesign. tx voting,  vote for %s, from %s \n",
                          __func__,
                          vote.choice.subject.GetHex(),
                          vote.voter.GetHex());
                return misbehavingErr();
            }
            LogPrintf("%s: Ignoring duplicating transaction vote \n", __func__);
            return {};
        }

        txVoting.emplace(vote.voter, vote);

        if (vote.tip != tip) {
            return {};
        }

        // TODO request tx if missed

        return doRoundVoting();
    }

    Output applyRoundVote(const CRoundVote& vote)
    {
        LogPrintf("%s: Received round vote for %s, from %s \n",
                  __func__,
                  vote.choice.subject.GetHex(),
                  vote.voter.GetHex());
        auto& roundVoting = v[tip].roundVotes[vote.nRound];

        // Check misbehaving or duplicating
        if (roundVoting.count(vote.voter) > 0) {
            if (roundVoting[vote.voter] != vote) {
                LogPrintf("%s: MISBEHAVING MASTERNODE! doublesign. round voting, vote for %s, from %s \n",
                          __func__,
                          vote.choice.subject.GetHex(),
                          vote.voter.GetHex());
                return misbehavingErr();
            }
            LogPrintf("%s: Ignoring duplicating Round vote \n", __func__);
            return {};
        }
        if (vote.choice.decision == CVoteChoice::Decision::PASS && vote.choice.subject != uint256{}) {
            LogPrintf("%s: MISBEHAVING MASTERNODE! malformed vote subject. round voting, vote for %s, from %s \n",
                      __func__,
                      vote.choice.subject.GetHex(),
                      vote.voter.GetHex());
            return misbehavingErr();
        }
        if (vote.choice.decision == CVoteChoice::Decision::NO) {
            LogPrintf("%s: MISBEHAVING MASTERNODE! malformed vote decision, vote for %s, from %s \n",
                      __func__,
                      vote.choice.subject.GetHex(),
                      vote.voter.GetHex());
            return misbehavingErr();
        }

        roundVoting.emplace(vote.voter, vote);

        Output out{};
        // check voting result after emplaced
        const auto stats = calcRoundVotingStats(vote.nRound);
        if (checkRoundStalemate(stats)) {
            LogPrintf("%s: New round ... %d \n", __func__, getCurrentRound());
            // on new round
        }
        else {
            const auto best_it = std::max_element(stats.pro.begin(), stats.pro.end());
            if (best_it != stats.pro.end() && best_it->second > 66) {
                LogPrintf("%s: Submit block ... \n", __func__);
                // out += block with votes
            }
        }

        if (vote.tip != tip) {
            return out;
        }

        return out + doRoundVoting();
    }

    Output doRoundVoting()
    {
        if (!amIvoter) {
            return {};
        }

        Output out{};

        const Round nRound = getCurrentRound();
        auto stats = calcRoundVotingStats(nRound);

        if (!wasVotedByMe_round(nRound) && !hasAnyUnfinishedTxs(nRound)) {
            using BlockVotes = std::pair<size_t, arith_uint256>;

            std::vector<BlockVotes> sortedViceBlocks{};

            // fill sortedViceBlocks
            for (const auto& viceBlock_p : v[tip].viceBlocks) {
                sortedViceBlocks.emplace_back(stats.pro[viceBlock_p.first], UintToArith256(viceBlock_p.first));
            }

            // sort the vice-blocks by number of votes, vice-block Hash (decreasing)
            std::sort(sortedViceBlocks.begin(), sortedViceBlocks.end(), [](const BlockVotes& l, const BlockVotes& r)
            {
                if (l.first == r.first)
                    return l.second < r.second;
                return l.first < r.first;
            });

            // vote for block
            const auto committedTxs = listCommittedTxs();
            boost::optional<BlockHash> viceBlockToVote{};
            for (const auto& viceBlock_p : sortedViceBlocks) {
                const BlockHash viceBlock = ArithToUint256(viceBlock_p.second);
                if (v[tip].viceBlocks.count(viceBlock) == 0) {
                    continue; // TODO request vice-block if missed
                }

                if (world.validateBlock(v[tip].viceBlocks[viceBlock], committedTxs, true)) {
                    viceBlockToVote = {viceBlock};
                    break;
                }
            }

            if (viceBlockToVote) {
                LogPrintf("%s: Vote for vice block %s at round %d \n", __func__, viceBlockToVote->GetHex(), nRound);

                CDposVote newVote{};
                newVote.voter = me;
                newVote.choice = {*viceBlockToVote, CVoteChoice::Decision::YES};
                newVote.nRound = nRound;
                newVote.tip = tip;
                out.vRoundVotes.push_back(newVote);

                out += applyRoundVote(newVote);
            }
            else {
                LogPrintf("%s: Can't find sutable vice block \n", __func__);
            }
        }
        else {
            LogPrintf("%s: Can't for vote vice block %d %d \n",
                      __func__,
                      wasVotedByMe_round(nRound),
                      hasAnyUnfinishedTxs(nRound));
        }
        return out;
    }

    Output doTxsVoting()
    {
        if (!amIvoter) {
            return {};
        }
        Output out{};
        LogPrintf("%s \n", __func__);
        for (const auto& tx_p : v[tip].txs) {
            out += voteForTx(tx_p.second);
        }
        return out;
    }

    Output onRoundTooLong()
    {
        if (!amIvoter) {
            return {};
        }
        const Round nRound = getCurrentRound();
        Output out{};
        LogPrintf("%s \n", __func__);
        if (!wasVotedByMe_round(nRound)) {
            CDposVote newVote{};
            newVote.voter = me;
            newVote.choice = {uint256{}, CVoteChoice::Decision::PASS};
            newVote.nRound = nRound;
            newVote.tip = tip;
            out.vRoundVotes.push_back(newVote);

            out += applyRoundVote(newVote);
        }
        return out;
    }

    Round getCurrentRound() const
    {
        for (Round i = 1;; i++) {
            const auto stats = calcRoundVotingStats(i);
            if (!checkRoundStalemate(stats))
                return i;
        }
        return 0; // not reachable
    }

    std::map<TxIdSorted, CTransaction> listCommittedTxs() const
    {
        const Round nRound = getCurrentRound();
        std::map<TxIdSorted, CTransaction> res{};
        for (const auto& tx_p : v[tip].txs) {
            const auto stats = calcTxVotingStats(tx_p.first, nRound);

            if (stats.pro > 66) {
                res.emplace(UintToArith256(tx_p.first), tx_p.second);
            }
        }

        return res;
    }

private:

    Output misbehavingErr() const
    {
        Output out;
        out.invalidState = true;
        return out;
    }

    Output voteForTx(const CTransaction& tx)
    {
        if (!amIvoter) {
            return {};
        }
        TxId txid = tx.GetHash();
        Output out{};

        const Round nRound = getCurrentRound();
        if (!wasVotedByMe_tx(txid, nRound)) {
            CVoteChoice::Decision decision{CVoteChoice::Decision::YES};

            auto myTxs = listApprovedByMe_txs();
            myTxs.emplace(UintToArith256(txid), tx);
            if (!world.validateTxs(myTxs)) { // check against my list
                decision = CVoteChoice::Decision::NO;
            }
            else { // check against committed list
                auto committedTxs = listCommittedTxs();
                committedTxs.emplace(UintToArith256(txid), tx);
                if (!world.validateTxs(committedTxs)) {
                    decision = CVoteChoice::Decision::NO;
                }
            }

            if (decision == CVoteChoice::Decision::YES && wasVotedByMe_round(nRound)) {
                decision = CVoteChoice::Decision::PASS;
            }
            if (decision == CVoteChoice::Decision::YES && wasVotedByMe_round(nRound)) {
                decision = CVoteChoice::Decision::PASS;
            }

            CDposVote newVote{};
            newVote.voter = me;
            newVote.nRound = nRound;
            newVote.tip = tip;
            newVote.choice = CVoteChoice{txid, decision};
            out.vTxVotes.push_back(newVote);

            out += applyTxVote(newVote);
        }
        return out;
    }

    Output onNewRound()
    {
        LogPrintf("%s \n", __func__);
    }

    // on new committed tx -> nothing

    bool wasVotedByMe_tx(TxId txid, Round nRound) const
    {
        // check specified round
        {
            const size_t roundFromMe = v[tip].txVotes[nRound][txid].count(me);
            if (roundFromMe > 0)
                return true;
        }

        // check Yes and No votes on other rounds. Such votes are active for all the rounds.
        for (const auto& txRoundVoting_p: v[tip].txVotes) {
            auto txVoting = v[tip].txVotes[nRound][txid];
            auto myVote_it = txVoting.find(me);

            if (myVote_it != txVoting.end()) {
                if (myVote_it->second.choice.decision != CVoteChoice::Decision::PASS) {
                    return true;
                }
            }
        }

        return false;
    }

    bool wasVotedByMe_round(Round nRound) const
    {
        return v[tip].roundVotes[nRound].count(me) > 0;
    }

    std::map<TxIdSorted, CTransaction> listApprovedByMe_txs() const
    {
        std::map<TxIdSorted, CTransaction> res{};

        for (const auto& txRoundVoting_p : v[tip].txVotes) {
            if (txRoundVoting_p.second.count(me) == 0) {
                continue;
            }
            for (const auto& myVotes_p : txRoundVoting_p.second.at(me)) {
                // Do these sanity checks only here, no need to copy-paste them
                assert(myVotes_p.second.nRound == txRoundVoting_p.first);
                assert(myVotes_p.second.tip == tip);
                assert(myVotes_p.second.voter == me);

                if (myVotes_p.second.choice.decision == CVoteChoice::Decision::YES) {
                    const TxId txid = myVotes_p.second.choice.subject;

                    if (v[tip].txs.count(txid) == 0) {
                        // theoretically can happen after reindex, if we didn't download all the txs
                        // TODO request tx
                        LogPrintf("%s didn't found approved tx in the map of txs \n", __func__);
                        continue;
                    }

                    const CTransaction tx = v[tip].txs[txid];

                    res.emplace(UintToArith256(txid), tx);
                }
            }
        }
    }

    TxVotingDistribution calcTxVotingStats(TxId txid, Round nRound) const
    {
        TxVotingDistribution stats{};

        for (const auto& roundVoting_p : v[tip].roundVotes) {
            for (const auto& vote_p : roundVoting_p.second) {
                const auto& vote = vote_p.second;
                switch (vote.choice.decision) {
                    case CVoteChoice::Decision::YES : stats.pro++;
                        break;
                    case CVoteChoice::Decision::NO : stats.contra++;
                        break;
                    case CVoteChoice::Decision::PASS :
                        if (vote.choice.subject == txid)
                            stats.abstinendi++;
                        break;
                }
            }
        }

        return stats;
    }

    RoundVotingDistribution calcRoundVotingStats(Round nRound) const
    {
        RoundVotingDistribution stats{};

        for (const auto& vote_p : v[tip].roundVotes[nRound]) {
            const auto& vote = vote_p.second;
            // Do these sanity checks only here, no need to copy-paste them
            assert(vote.nRound == nRound);
            assert(vote.tip == tip);
            assert(vote.choice.decision != CVoteChoice::Decision::NO);

            switch (vote.choice.decision) {
                case CVoteChoice::Decision::YES : stats.pro[vote.choice.subject]++;
                    break;
                case CVoteChoice::Decision::PASS : stats.abstinendi++;
                    assert(vote.choice.subject == uint256{});
                    break;
            }
        }

        return stats;
    }

    bool atLeastOneViceBlockIsValid(Round nRound, int decision) const
    {
        if (v[tip].viceBlocks.empty())
            return false;

        const auto committedTxs = listCommittedTxs();

        for (const auto& viceBlock_p : v[tip].viceBlocks) {
            if (world.validateBlock(viceBlock_p.second, committedTxs, true)) {
                return true;
            }
        }
        return false;
    }

    bool hasAnyUnfinishedTxs(Round nRound) const
    {
        return false;
    }

    bool txHasAnyVote(TxId txid) const
    {
        bool found = false;
        for (const auto& roundVoting_p : v[tip].txVotes) {
            for (const auto& txVoting_p : roundVoting_p.second) {
                for (const auto& vote_p : txVoting_p.second) {
                    const auto& vote = vote_p.second;
                    if (vote.choice.subject == txid) {
                        found = true;
                        break;
                    }
                }
            }
        }
        return found;
    }

    bool wasTxLost(TxId txid) const
    {
        if (v[tip].txs.count(txid) != 0) // known
            return false;
        return txHasAnyVote(txid);
    }

    bool checkRoundStalemate(const RoundVotingDistribution& stats) const
    {
        const size_t totus = stats.totus();
        const size_t notKnown = 100 - totus;

        const auto best_it = std::max_element(stats.pro.begin(), stats.pro.end());
        const size_t nBest = (best_it != stats.pro.end()) ? best_it->second : 0;

        return (nBest + notKnown) < 66;
    }

    bool checkTxNotCommittable(const TxVotingDistribution& stats) const
    {
        const size_t totus = stats.totus();
        const size_t notKnown = 100 - totus;

        return (stats.pro + notKnown) < 66;
    }

    std::map<BlockHash, VotingState>& v;

    CMasternode::ID me;
    BlockHash tip;

    Callbacks world;

    size_t minQuorum;
    size_t numOfVoters;

    bool amIvoter = false;
};

}

#endif //DPOS_VOTER_H
