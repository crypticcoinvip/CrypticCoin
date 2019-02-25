// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "dpos_voter.h"
#include <boost/range/numeric.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm/copy.hpp>

namespace dpos
{

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

CDposVoterOutput& CDposVoterOutput::operator+=(const CDposVoterOutput& r)
{
    std::copy(r.vTxVotes.begin(), r.vTxVotes.end(), std::back_inserter(this->vTxVotes));
    std::copy(r.vRoundVotes.begin(), r.vRoundVotes.end(), std::back_inserter(this->vRoundVotes));
    std::copy(r.vErrors.begin(), r.vErrors.end(), std::back_inserter(this->vErrors));
    if (r.blockToSubmit) {
        this->blockToSubmit = r.blockToSubmit;
    }
    return *this;
}

CDposVoterOutput CDposVoterOutput::operator+(const CDposVoterOutput& r)
{
    CDposVoterOutput res = *this;
    res += r;
    return res;
}

bool CDposVoterOutput::empty() const
{
    if (!vTxVotes.empty())
        return false;
    if (!vRoundVotes.empty())
        return false;
    if (!vErrors.empty())
        return false;
    if (blockToSubmit)
        return false;
    return true;
}

size_t CRoundVotingDistribution::totus() const
{
    return boost::accumulate(pro | boost::adaptors::map_values, 0) + abstinendi;
}

size_t CTxVotingDistribution::totus() const
{
    return pro + contra + abstinendi;
}

void CDposVoter::setVoting(BlockHash tip, Callbacks world, bool amIvoter, CMasternode::ID me)
{
    this->tip = tip;
    this->world = std::move(world);
    this->amIvoter = amIvoter;
    this->me = me;
}

void CDposVoter::updateTip(BlockHash tip)
{
    this->tip = tip;
}

CDposVoter::Output CDposVoter::applyViceBlock(const CBlock& viceBlock)
{
    if (!world.validateBlock(viceBlock, {}, false)) {
        return misbehavingErr("vice-block validation failed");
    }

    if (viceBlock.hashPrevBlock != tip && !world.allowArchiving(viceBlock.hashPrevBlock)) {
        LogPrintf("%s: Ignoring too old vice-block: %s \n", __func__, viceBlock.GetHash().GetHex());
        return {};
    }

    if (!v[tip].viceBlocks.emplace(viceBlock.GetHash(), viceBlock).second) {
        LogPrintf("%s: Ignoring duplicating vice-block: %s \n", __func__, viceBlock.GetHash().GetHex());
        return {};
    }

    if (viceBlock.nRound != getCurrentRound()) {
        LogPrintf("%s: Ignoring vice-block from prev. round: %s \n", __func__, viceBlock.GetHash().GetHex());
        return {};
    }

    LogPrintf("%s: Received vice-block %s \n", __func__, viceBlock.GetHash().GetHex());
    return doRoundVoting();
}

CDposVoter::Output CDposVoter::applyTx(const CTransaction& tx)
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

CDposVoter::Output CDposVoter::applyTxVote(const CTxVote& vote)
{
    if (vote.tip != tip && !world.allowArchiving(vote.tip)) {
        LogPrintf("%s: Ignoring too old transaction vote from block %s \n", __func__, vote.tip.GetHex());
        return {};
    }

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
            return misbehavingErr("masternode tx doublesign misbehaving");
        }
        LogPrintf("%s: Ignoring duplicating transaction vote \n", __func__);
        return {};
    }

    txVoting.emplace(vote.voter, vote);

    if (vote.tip != tip) {
        return {};
    }

    // TODO request tx if missed OR vote PASS on this tx

    return doRoundVoting();
}

CDposVoter::Output CDposVoter::applyRoundVote(const CRoundVote& vote)
{
    if (vote.tip != tip && !world.allowArchiving(vote.tip)) {
        LogPrintf("%s: Ignoring too old round vote from block %s \n", __func__, vote.tip.GetHex());
        return {};
    }

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
            return misbehavingErr("masternode round doublesign misbehaving");
        }
        LogPrintf("%s: Ignoring duplicating Round vote \n", __func__);
        return {};
    }
    if (vote.choice.decision == CVoteChoice::Decision::PASS && vote.choice.subject != uint256{}) {
        LogPrintf("%s: MISBEHAVING MASTERNODE! malformed vote subject. round voting, vote for %s, from %s \n",
                  __func__,
                  vote.choice.subject.GetHex(),
                  vote.voter.GetHex());
        return misbehavingErr("malformed vote subject");
    }
    if (vote.choice.decision == CVoteChoice::Decision::NO) {
        LogPrintf("%s: MISBEHAVING MASTERNODE! malformed vote decision, vote for %s, from %s \n",
                  __func__,
                  vote.choice.subject.GetHex(),
                  vote.voter.GetHex());
        return misbehavingErr("malformed vote decision");
    }

    roundVoting.emplace(vote.voter, vote);

    Output out{};
    if (vote.tip != tip) {
        return out;
    }

    // check voting result after emplaced
    const auto stats = calcRoundVotingStats(vote.nRound);
    if (checkRoundStalemate(stats)) {
        LogPrintf("%s: New round ... %d \n", __func__, getCurrentRound());
        // on new round
        out += doTxsVoting();
    }
    out += doRoundVoting();
    if (vote.choice.decision == CVoteChoice::Decision::YES) {
        out += tryToSubmitBlock(vote.choice.subject);
    }

    return out;
}

void CDposVoter::pruneTxVote(const CTxVote& vote)
{
    for (auto&& pair : v[vote.tip].txVotes[vote.nRound]) {
        const auto vote_it = pair.second.find(vote.voter);
        if (vote_it != pair.second.end() && vote_it->second == vote) {
            pair.second.erase(vote_it);
        }
    }
}

CDposVoter::Output CDposVoter::doRoundVoting()
{
    if (!amIvoter) {
        return {};
    }

    Output out{};

    const Round nRound = getCurrentRound();
    auto stats = calcRoundVotingStats(nRound);

    if (!wasVotedByMe_round(nRound) && !haveAnyUnfinishedTxs(nRound)) {
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
            const BlockHash viceBlockId = ArithToUint256(viceBlock_p.second);
            if (v[tip].viceBlocks.count(viceBlockId) == 0) {
                continue; // TODO request vice-block if missed
            }

            const CBlock& viceBlock = v[tip].viceBlocks[viceBlockId];
            if (viceBlock.nRound == nRound && world.validateBlock(viceBlock, committedTxs, true)) {
                viceBlockToVote = {viceBlockId};
                break;
            }
        }

        if (viceBlockToVote) {
            LogPrintf("%s: Vote for vice block %s at round %d \n", __func__, viceBlockToVote->GetHex(), nRound);

            CRoundVote newVote{};
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
                  haveAnyUnfinishedTxs(nRound));
    }
    return out;
}

CDposVoter::Output CDposVoter::tryToSubmitBlock(BlockHash viceBlockId)
{
    Output out{};
    const Round nCurrentRound = getCurrentRound();
    auto stats = calcRoundVotingStats(nCurrentRound);

    if (stats.pro[viceBlockId] >= minQuorum) {
        const auto& viceBlock = v[tip].viceBlocks[viceBlockId];
        if (viceBlock.nRound != nCurrentRound) {
            return out;
        }
        if (!world.validateBlock(viceBlock, listCommittedTxs(), true)) {
            return out;
        }

        LogPrintf("%s: Submit block ... \n", __func__);
        CBlockToSubmit blockToSubmit;
        blockToSubmit.block = viceBlock;
        const auto& approvedBy_m = v[tip].roundVotes[nCurrentRound];
        // Retrieve all keys
        boost::copy(approvedBy_m | boost::adaptors::map_keys, std::back_inserter(blockToSubmit.vApprovedBy));

        out.blockToSubmit = {blockToSubmit};
    }

    return out;
}

CDposVoter::Output CDposVoter::doTxsVoting()
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

CDposVoter::Output CDposVoter::onRoundTooLong()
{
    if (!amIvoter) {
        return {};
    }
    const Round nRound = getCurrentRound();
    Output out{};
    LogPrintf("%s \n", __func__);
    if (!wasVotedByMe_round(nRound)) {
        CRoundVote newVote{};
        newVote.voter = me;
        newVote.choice = {uint256{}, CVoteChoice::Decision::PASS};
        newVote.nRound = nRound;
        newVote.tip = tip;
        out.vRoundVotes.push_back(newVote);

        out += applyRoundVote(newVote);
    }
    return out;
}

bool CDposVoter::checkAmIVoter() const
{
    return this->amIvoter;
}

Round CDposVoter::getCurrentRound() const
{
    for (Round i = 1;; i++) {
        const auto stats = calcRoundVotingStats(i);
        if (!checkRoundStalemate(stats))
            return i;
    }
    return 0; // not reachable
}

std::map<TxIdSorted, CTransaction> CDposVoter::listCommittedTxs() const
{
    const Round nRound = getCurrentRound();
    std::map<TxIdSorted, CTransaction> res{};
    for (const auto& tx_p : v.at(tip).txs) {
        const auto stats = calcTxVotingStats(tx_p.first, nRound);

        if (stats.pro >= minQuorum) {
            res.emplace(UintToArith256(tx_p.first), tx_p.second);
        }
    }

    return res;
}

bool CDposVoter::isCommittedTx(const CTransaction& tx) const
{
    const Round nRound = getCurrentRound();
    const TxId txid = tx.GetHash();
    const auto stats = calcTxVotingStats(txid, nRound);

    return stats.pro >= minQuorum;
}

bool CDposVoter::isTxApprovedByMe(const CTransaction& tx) const
{
    auto myTxs = listApprovedByMe_txs();
    return myTxs.count(UintToArith256(tx.GetHash())) != 0;
}

CDposVoter::Output CDposVoter::misbehavingErr(const std::string& msg) const
{
    Output out;
    out.vErrors.push_back(msg);
    return out;
}

CDposVoter::Output CDposVoter::voteForTx(const CTransaction& tx)
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
        if (decision == CVoteChoice::Decision::YES && atLeastOneViceBlockIsValid(nRound)) {
            decision = CVoteChoice::Decision::PASS;
        }

        CTxVote newVote{};
        newVote.voter = me;
        newVote.nRound = nRound;
        newVote.tip = tip;
        newVote.choice = CVoteChoice{txid, decision};
        out.vTxVotes.push_back(newVote);

        out += applyTxVote(newVote);
    }
    return out;
}

bool CDposVoter::wasVotedByMe_tx(TxId txid, Round nRound) const
{
    // check specified round
    {
        const size_t roundFromMe = v[tip].txVotes[nRound][txid].count(me);
        if (roundFromMe > 0)
            return true;
    }

    // check Yes and No votes on other rounds. Such votes are active for all the rounds.
    for (auto&& txRoundVoting_p: v[tip].txVotes) {
        auto txVoting = txRoundVoting_p.second[txid];
        auto myVote_it = txVoting.find(me);

        if (myVote_it != txVoting.end()) {
            if (myVote_it->second.choice.decision != CVoteChoice::Decision::PASS) {
                return true;
            }
        }
    }

    return false;
}

bool CDposVoter::wasVotedByMe_round(Round nRound) const
{
    return v[tip].roundVotes[nRound].count(me) > 0;
}

std::map<TxIdSorted, CTransaction> CDposVoter::listApprovedByMe_txs() const
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

    return res;
}

CTxVotingDistribution CDposVoter::calcTxVotingStats(TxId txid, Round nRound) const
{
    CTxVotingDistribution stats{};

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

CRoundVotingDistribution CDposVoter::calcRoundVotingStats(Round nRound) const
{
    CRoundVotingDistribution stats{};

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

bool CDposVoter::atLeastOneViceBlockIsValid(Round nRound) const
{
    if (v[tip].viceBlocks.empty())
        return false;

    const auto committedTxs = listCommittedTxs();

    for (const auto& viceBlock_p : v[tip].viceBlocks) {
        const CBlock& viceBlock = viceBlock_p.second;
        if (viceBlock.nRound == nRound && world.validateBlock(viceBlock, committedTxs, true)) {
            return true;
        }
    }
    return false;
}

bool CDposVoter::txHasAnyVote(TxId txid) const
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

bool CDposVoter::wasTxLost(TxId txid) const
{
    if (v[tip].txs.count(txid) != 0) // known
        return false;
    return txHasAnyVote(txid);
}

bool CDposVoter::checkRoundStalemate(const CRoundVotingDistribution& stats) const
{
    const size_t totus = stats.totus();
    const size_t notKnown = numOfVoters - totus;

    const auto best_it = std::max_element(stats.pro.begin(), stats.pro.end());
    const size_t nBest = (best_it != stats.pro.end()) ? best_it->second : 0;

    // no winner, and no winner possible
    return (nBest + notKnown) < minQuorum;
}

bool CDposVoter::checkTxNotCommittable(const CTxVotingDistribution& stats) const
{
    const size_t totus = stats.totus();
    const size_t notKnown = numOfVoters - totus;

    // not committed, and not possible to commit
    return (stats.pro + notKnown) < minQuorum;
}

bool CDposVoter::haveAnyUnfinishedTxs(Round nRound) const
{
    for (const auto& tx_p : v[tip].txs) {
        const auto stats = calcTxVotingStats(tx_p.first, nRound);

        const bool notCommittable = checkTxNotCommittable(stats);
        const bool notVoted = stats.totus() == 0;
        const bool committed = stats.pro >= minQuorum;

        const bool finished = notVoted || notCommittable || committed;
        if (!finished) {
            return true;
        }
    }
    return false;
}

} // namespace dpos
