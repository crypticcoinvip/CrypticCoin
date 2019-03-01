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

bool operator==(const CTxVote& l, const CTxVote& r)
{
    return static_cast<const CDposVote&>(l) == static_cast<const CDposVote&>(r);
}

bool operator!=(const CTxVote& l, const CTxVote& r)
{
    return !(l == r);
}

bool operator==(const CRoundVote& l, const CRoundVote& r)
{
    return static_cast<const CDposVote&>(l) == static_cast<const CDposVote&>(r);
}

bool operator!=(const CRoundVote& l, const CRoundVote& r)
{
    return !(l == r);
}

CDposVoterOutput& CDposVoterOutput::operator+=(const CDposVoterOutput& r)
{
    std::copy(r.vTxVotes.begin(), r.vTxVotes.end(), std::back_inserter(this->vTxVotes));
    std::copy(r.vRoundVotes.begin(), r.vRoundVotes.end(), std::back_inserter(this->vRoundVotes));
    std::copy(r.vTxReqs.begin(), r.vTxReqs.end(), std::back_inserter(this->vTxReqs));
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
    if (!vTxReqs.empty())
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

CDposVoter::CDposVoter(Callbacks world)
{
    this->world = std::move(world);
}

void CDposVoter::setVoting(bool amIvoter, CMasternode::ID me)
{
    this->amIvoter = amIvoter;
    this->me = me;
}

void CDposVoter::updateTip(BlockHash tip)
{
    LogPrintf("%s: Change current tip from %s to %s\n", __func__, this->tip.GetHex(), tip.GetHex());

    // tip is changed not first time. TODO filter only after N blocks. store txs to clear here
    if (this->tip != BlockHash{} && this->tip != tip)
        filterFinishedTxs(this->txs, 0);

    this->tip = tip;
}

CDposVoter::Output CDposVoter::applyViceBlock(const CBlock& viceBlock)
{
    if (viceBlock.nRound <= 0 || !viceBlock.vSig.empty() || !viceBlock.hashReserved1.IsNull() || !viceBlock.hashReserved2.IsNull()) {
        return misbehavingErr("vice-block is malformed");
    }
    if (!world.validateBlock(viceBlock, {}, false)) {
        return misbehavingErr("vice-block validation failed");
    }

    if (viceBlock.hashPrevBlock != tip && !world.allowArchiving(viceBlock.hashPrevBlock)) {
        LogPrintf("%s: Ignoring too old vice-block: %s \n", __func__, viceBlock.GetHash().GetHex());
        return {};
    }

    if (!v[viceBlock.hashPrevBlock].viceBlocks.emplace(viceBlock.GetHash(), viceBlock).second) {
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
    LogPrintf("%s: Received tx %s \n", __func__, txid.GetHex());

    const bool wasLost = wasTxLost(txid);
    txs[txid] = tx;

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
    if (vote.nRound <= 0 || !vote.choice.isStandardDecision()) {
        return misbehavingErr("masternode malformed tx vote");
    }

    if (vote.tip != tip && !world.allowArchiving(vote.tip)) {
        LogPrintf("%s: Ignoring too old transaction vote from block %s \n", __func__, vote.tip.GetHex());
        return {};
    }

    const TxId txid = vote.choice.subject;
    LogPrintf("%s: Received transaction vote for %s, from %s, decision=%d \n",
              __func__,
              txid.GetHex(),
              vote.voter.GetHex(),
              vote.choice.decision);
    auto& txVoting = v[vote.tip].txVotes[vote.nRound][txid];

    // Check misbehaving or duplicating
    if (txVoting.count(vote.voter) > 0) {
        if (txVoting[vote.voter] != vote) {
            LogPrintf("%s: MISBEHAVING MASTERNODE! doublesign. tx voting,  vote for %s, from %s \n",
                      __func__,
                      txid.GetHex(),
                      vote.voter.GetHex());
            return misbehavingErr("masternode tx doublesign misbehaving");
        }
        LogPrintf("%s: Ignoring duplicating transaction vote \n", __func__);
        return {};
    }

    txVoting.emplace(vote.voter, vote);

    if (vote.tip != tip) { // if vote isn't for current tip, voting state didn't chagne, and I don't need to do voting
        return {};
    }

    Output out;
    if (txs.count(txid) == 0) {
        // request the missing tx
        out.vTxReqs.push_back(txid);
    }

    return out + doRoundVoting();
}

CDposVoter::Output CDposVoter::applyRoundVote(const CRoundVote& vote)
{
    if (vote.nRound <= 0 || !vote.choice.isStandardDecision()) {
        return misbehavingErr("masternode malformed round vote");
    }
    if (vote.choice.decision == CVoteChoice::Decision::PASS && vote.choice.subject != uint256{}) {
        return misbehavingErr("masternode malformed vote subject");
    }
    if (vote.choice.decision == CVoteChoice::Decision::NO) {
        return misbehavingErr("masternode malformed vote decision");
    }

    if (vote.nRound <= 0) {
        LogPrintf("%s: MISBEHAVING MASTERNODE! malformed vote from %s \n",
                  __func__,
                  vote.voter.GetHex());
        return misbehavingErr("masternode malformed tx vote");
    }

    if (vote.tip != tip && !world.allowArchiving(vote.tip)) {
        LogPrintf("%s: Ignoring too old round vote from block %s \n", __func__, vote.tip.GetHex());
        return {};
    }

    LogPrintf("%s: Received round vote for %s, from %s \n",
              __func__,
              vote.choice.subject.GetHex(),
              vote.voter.GetHex());
    auto& roundVoting = v[vote.tip].roundVotes[vote.nRound];

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
        out += doRoundVoting();
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

    auto myTxs = listApprovedByMe_txs();
    if (!myTxs.missing.empty()) {
        // forbid voting if one of approved by me txs is missing.
        // It means that I can't check that a tx doesn't interfere with already approved by me.
        // Without this condition, it's possible to do doublesign by accident.
        std::copy(myTxs.missing.begin(),
                  myTxs.missing.end(),
                  std::back_inserter(out.vTxReqs)); // request missing txs
        return out;
    }

    filterFinishedTxs(myTxs.txs, nRound); // filter finished tx. If all txs are finished, the result is empty
    if (!myTxs.txs.empty()) {
        LogPrintf("%s: Can't do round voting because %d of approved-by-me txs aren't finished (one of them is %s) \n",
                  __func__,
                  myTxs.txs.size(),
                  myTxs.txs.begin()->first.GetHex());
        return out;
    }

    if (wasVotedByMe_round(nRound)) {
        LogPrintf("%s: Round was already voted by me \n", __func__);
        return out;
    }

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
    // committed list may be not full, which is fine
    const auto committedTxs = listCommittedTxs();
    boost::optional<BlockHash> viceBlockToVote{};
    for (const auto& viceBlock_p : sortedViceBlocks) {
        const BlockHash viceBlockId = ArithToUint256(viceBlock_p.second);
        if (v[tip].viceBlocks.count(viceBlockId) == 0) {
            continue; // TODO request vice-block if missing
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
        LogPrintf("%s: Suitable vice block wasn't found at round %d, candidates=%d \n", __func__, nRound, sortedViceBlocks.size());
    }

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

    if (wasVotedByMe_tx(txid, nRound)) {
        LogPrintf("%s: Tx %s was already voted by me \n", __func__, txid.GetHex());
        return out;
    }

    CVoteChoice::Decision decision{CVoteChoice::Decision::YES};

    auto myTxs = listApprovedByMe_txs();
    if (!myTxs.missing.empty()) {
        // forbid voting if one of approved by me txs is missing.
        // It means that I can't check that a tx doesn't interfere with already approved by me.
        // Without this condition, it's possible to do doublesign by accident.
        std::copy(myTxs.missing.begin(),
                  myTxs.missing.end(),
                  std::back_inserter(out.vTxReqs)); // request missing txs
        return out;
    }

    myTxs.txs.emplace(UintToArith256(txid), tx);
    if (!world.validateTxs(myTxs.txs)) { // check against my list
        decision = CVoteChoice::Decision::NO;
    }
    else {
        // check against committed list. Strictly we need to to check only against my list,
        // but checking against committed txs will speed up the consensus.
        // committed list may be not full, which is fine
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

    return out;
}

CDposVoter::Output CDposVoter::tryToSubmitBlock(BlockHash viceBlockId)
{
    Output out{};
    const Round nCurrentRound = getCurrentRound();
    auto stats = calcRoundVotingStats(nCurrentRound);

    if (stats.pro[viceBlockId] >= minQuorum) {
        if (v[tip].viceBlocks.count(viceBlockId) == 0) {
            return out;
        }
        const auto& viceBlock = v[tip].viceBlocks[viceBlockId];
        if (viceBlock.nRound != nCurrentRound) {
            return out;
        }
        // committed list may be not full, which is fine
        if (!world.validateBlock(viceBlock, listCommittedTxs(), true)) {
            return out;
        }

        LogPrintf("%s: Submit block, num of votes = %d, minQuorum = %d \n",
                  __func__,
                  stats.pro[viceBlockId],
                  minQuorum);
        CBlockToSubmit blockToSubmit;
        blockToSubmit.block = viceBlock;
        const auto& approvedBy_m = v[tip].roundVotes[nCurrentRound];
        // Retrieve all keys
        boost::copy(approvedBy_m | boost::adaptors::map_keys,
                    std::back_inserter(blockToSubmit.vApprovedBy)); // TODO filter pass votes

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
    for (const auto& tx_p : txs) {
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
    LogPrintf("%s: %d\n", __func__, nRound);
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

const BlockHash& CDposVoter::getTip() const
{
    return this->tip;
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
    for (const auto& tx_p : txs) {
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
    const auto myTxs = listApprovedByMe_txs();
    const TxId txid = tx.GetHash();
    return myTxs.txs.count(UintToArith256(txid)) != 0 || myTxs.missing.count(txid) != 0;
}

CDposVoter::Output CDposVoter::misbehavingErr(const std::string& msg) const
{
    Output out;
    out.vErrors.push_back(msg);
    return out;
}

bool CDposVoter::wasVotedByMe_tx(TxId txid, Round nRound) const
{
    // check specified round
    {
        if (v[tip].txVotes.count(nRound) != 0) {
            const size_t roundFromMe = v[tip].txVotes[nRound][txid].count(me);
            if (roundFromMe > 0)
                return true;
        }
    }

    // check Yes and No votes on other rounds. Such votes are active for all the rounds.
    for (auto&& txRoundVoting_p: v[tip].txVotes) {
        if (txRoundVoting_p.second.count(txid) == 0)
            continue;

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
    return v[tip].roundVotes.count(nRound) > 0 && v[tip].roundVotes[nRound].count(me) > 0;
}

CDposVoter::ApprovedByMeTxsList CDposVoter::listApprovedByMe_txs() const
{
    ApprovedByMeTxsList res{};

    for (auto&& txsRoundVoting_p : v[tip].txVotes) {
        for (auto&& txVoting_p : txsRoundVoting_p.second) {
            // don't insert empty element if empty
            if (txVoting_p.second.count(me) == 0)
                continue;
            // search for my vote
            const CTxVote myVote = txVoting_p.second[me];
            if (myVote.choice.decision == CVoteChoice::Decision::YES) {
                const TxId txid = myVote.choice.subject;

                if (txs.count(txid) == 0) {
                    // theoretically can happen after reindex, if we didn't download all the txs
                    LogPrintf("%s approved tx=%d wasn't found in the map of txs \n", __func__, txid.GetHex());
                    res.missing.insert(txid);
                    continue;
                }

                const CTransaction tx = txs[txid];

                res.txs.emplace(UintToArith256(txid), tx);
            }
        }
    }

    return res;
}

CTxVotingDistribution CDposVoter::calcTxVotingStats(TxId txid, Round nRound) const
{
    CTxVotingDistribution stats{};

    for (auto&& txsRoundVoting_p : v[tip].txVotes) {
        // don't insert empty element if empty
        if (txsRoundVoting_p.second.count(txid) == 0)
            continue;
        // count votes
        for (const auto& vote_p : txsRoundVoting_p.second[txid]) {
            const auto& vote = vote_p.second;
            // Do these sanity checks only here, no need to copy-paste them
            assert(vote.nRound == txsRoundVoting_p.first);
            assert(vote.tip == tip);
            assert(vote.choice.subject == txid);

            switch (vote.choice.decision) {
                case CVoteChoice::Decision::YES : stats.pro++;
                    break;
                case CVoteChoice::Decision::NO : stats.contra++;
                    break;
                case CVoteChoice::Decision::PASS :
                    if (vote.nRound == nRound) // count PASS votes only from specified round
                        stats.abstinendi++;
                    break;
                default: assert(false);
                    break;
            }
        }
    }

    return stats;
}

CRoundVotingDistribution CDposVoter::calcRoundVotingStats(Round nRound) const
{
    CRoundVotingDistribution stats{};

    // don't insert empty element if empty
    if (v[tip].roundVotes.count(nRound) == 0)
        return stats;

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
            default: assert(false);
                break;
        }
    }

    return stats;
}

bool CDposVoter::atLeastOneViceBlockIsValid(Round nRound) const
{
    if (v[tip].viceBlocks.empty())
        return false;

    // committed list may be not full, which is fine
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
    for (const auto& txsRoundVoting_p : v[tip].txVotes) {
        const auto& txVoting_it = txsRoundVoting_p.second.find(txid);
        if (txVoting_it == txsRoundVoting_p.second.end()) // voting not found
            continue;
        if (txVoting_it->second.size() > 0) // voting isn't empty
            return true;
    }
    return false;
}

bool CDposVoter::wasTxLost(TxId txid) const
{
    if (txs.count(txid) != 0) // known
        return false;
    return txHasAnyVote(txid);
}

bool CDposVoter::checkRoundStalemate(const CRoundVotingDistribution& stats) const
{
    assert(numOfVoters > 0);
    assert(minQuorum <= numOfVoters);
    const size_t totus = stats.totus();
    const size_t notKnown = totus <= numOfVoters ? numOfVoters - totus : 0;

    const auto best_it = std::max_element(stats.pro.begin(), stats.pro.end(),
                                          [](const std::pair<BlockHash, size_t>& p1,
                                             const std::pair<BlockHash, size_t>& p2)
                                          {
                                              return p1.second < p2.second;
                                          });
    const size_t nBest = (best_it != stats.pro.end()) ? best_it->second : 0;

    // no winner, and no winner possible
    return (nBest + notKnown) < minQuorum;
}

bool CDposVoter::checkTxNotCommittable(const CTxVotingDistribution& stats) const
{
    assert(numOfVoters > 0);
    assert(minQuorum <= numOfVoters);
    const size_t totus = stats.totus();
    const size_t notKnown = totus <= numOfVoters ? numOfVoters - totus : 0;

    // not committed, and not possible to commit
    return (stats.pro + notKnown) < minQuorum;
}

void CDposVoter::filterFinishedTxs(std::map<TxIdSorted, CTransaction>& txs_f, Round nRound) const
{
    for (auto it = txs_f.begin(); it != txs_f.end();) {
        const TxId txid = ArithToUint256(it->first);
        auto stats = calcTxVotingStats(txid, nRound);
        if (nRound <= 0)
            stats.abstinendi = 0;

        const bool notCommittable = checkTxNotCommittable(stats);
        const bool committed = stats.pro >= minQuorum;
        const bool finished = notCommittable || committed;

        if (finished)
            it = txs_f.erase(it);
        else
            it++;
    }
}
void CDposVoter::filterFinishedTxs(std::map<TxId, CTransaction>& txs_f, Round nRound) const
{
    for (auto it = txs_f.begin(); it != txs_f.end();) {
        const TxId txid = it->first;
        auto stats = calcTxVotingStats(txid, nRound);
        if (nRound
            == 0) // round starts with 1, so we didn't accept any votes for round 0. yes/no votes could come from any round.
            assert(stats.abstinendi == 0);

        const bool notCommittable = checkTxNotCommittable(stats);
        const bool committed = stats.pro >= minQuorum;
        const bool finished = notCommittable || committed;

        if (finished)
            it = txs_f.erase(it);
        else
            it++;
    }
}

} // namespace dpos
