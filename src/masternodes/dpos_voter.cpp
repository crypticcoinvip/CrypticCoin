// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "dpos_voter.h"
#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/numeric.hpp>

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
    std::copy(r.vViceBlockReqs.begin(), r.vViceBlockReqs.end(), std::back_inserter(this->vViceBlockReqs));
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
    if (!vViceBlockReqs.empty())
        return false;
    if (!vErrors.empty())
        return false;
    if (blockToSubmit)
        return false;
    return true;
}

size_t CRoundVotingDistribution::totus() const
{
    return boost::accumulate(pro | boost::adaptors::map_values, 0);
}

size_t CTxVotingDistribution::totus() const
{
    return pro;
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
    // tip is changed
    if (this->tip == tip)
        return;

    LogPrintf("dpos: %s: Change current tip from %s to %s\n", __func__, this->tip.GetHex(), tip.GetHex());

    if (!verifyVotingState())
        assert(!"dPoS database is corrupted (voting state verification failed)! Please restart with -reindex to recover.");

    // filter txs without votes, so txs.size <= maxNotVotedTxsToKeep / 2
    for (auto it = this->txs.begin(); it != this->txs.end() && this->txs.size() > maxNotVotedTxsToKeep / 2;) {
        if (!txHasAnyVote(it->first, this->tip))
            it = this->pruneTx(it);
        else
            it++;
    }

    this->tip = tip;
}

CDposVoter::Output CDposVoter::applyViceBlock(const CBlock& viceBlock)
{
    if (viceBlock.nRound <= 0 || !viceBlock.vSig.empty()) {
        return misbehavingErr("vice-block is malformed");
    }

    if (v[viceBlock.hashPrevBlock].viceBlocks.count(viceBlock.GetHash()) > 0) {
        LogPrintf("dpos: %s: Ignoring duplicating vice-block: %s \n", __func__, viceBlock.GetHash().GetHex());
        return {};
    }

    if (!world.validateBlock(viceBlock, true)) {
        return misbehavingErr("vice-block PoW validation failed");
    }

    if (viceBlock.hashPrevBlock != tip && !world.allowArchiving(viceBlock.hashPrevBlock)) {
        LogPrintf("dpos: %s: Ignoring too old vice-block: %s \n", __func__, viceBlock.GetHash().GetHex());
        return {};
    }

    v[viceBlock.hashPrevBlock].viceBlocks.emplace(viceBlock.GetHash(), viceBlock);

    LogPrintf("dpos: %s: Received vice-block %s \n", __func__, viceBlock.GetHash().GetHex());
    return doRoundVoting();
}

std::vector<COutPoint> CDposVoter::getInputsOf(const CTransaction& tx) {
    std::vector<COutPoint> res;
    for (const auto& in : tx.vin) {
        res.push_back(in.prevout);
    }
    for (const auto& zIn : tx.vShieldedSpend) {
        COutPoint nullifier;
        nullifier.hash = zIn.nullifier;
        nullifier.n = Z_OUTPUT_INDEX;
        res.push_back(nullifier);
    }
    return res;
}

std::set<COutPoint> CDposVoter::getInputsOf_set(const CTransaction& tx) {
    std::set<COutPoint> res;
    const std::vector<COutPoint> v = getInputsOf(tx);
    for (const auto& in : v) {
        res.insert(in);
    }
    return res;
}

std::map<TxId, CTransaction>::iterator CDposVoter::pruneTx(std::map<TxId, CTransaction>::iterator tx_it) {
    if (tx_it != txs.end()) {
        const CTransaction& tx = tx_it->second;
        const std::vector<COutPoint> inputs = getInputsOf(tx);
        for (const auto& in : inputs) {
            // erase pledgedInputs index
            const auto& collisions_p = this->pledgedInputs.equal_range(in);
            for (auto colliTx_it = collisions_p.first; colliTx_it != collisions_p.second; colliTx_it++) {
                if (colliTx_it->second == tx.GetHash()) {
                    pledgedInputs.erase(colliTx_it);
                    break;
                }
            }
        }
        return txs.erase(tx_it);
    }
    return tx_it;
}

CDposVoter::Output CDposVoter::applyTx(const CTransaction& tx)
{
    assert(tx.fInstant);

    TxId txid = tx.GetHash();

    if (txs.count(txid) > 0) {
        return {};
    }

    const bool wasLost = wasTxLost(txid, tip);

    // don't pre-validate tx if it already has votes
    if (!wasLost && !world.preValidateTx(tx, VOTING_MEMORY * 2)) {
        LogPrintf("dpos: %s: Received invalid tx %s \n", __func__, txid.GetHex());
        pruneTx(txs.find(txid));
        return misbehavingErr("invalid tx");
    }

    Output out{};

    if (txs.size() < maxNotVotedTxsToKeep || wasLost) {
        txs[txid] = tx;
        if (wasLost) {
            LogPrintf("dpos: %s: Received requested tx %s \n", __func__, txid.GetHex());
            out += doRoundVoting();
            out += doTxsVoting();
            out += doRoundVoting();
        } else {
            LogPrintf("dpos: %s: Received tx %s \n", __func__, txid.GetHex());
            out += voteForTx(tx);
        }
    } else {
        LogPrintf("dpos: %s: Dropping tx without votes %s \n", __func__, txid.GetHex());
    }

    // update the index input -> txid
    if (wasLost) {
        const std::vector<COutPoint> inputs = getInputsOf(tx);
        for (const auto& in : inputs) {
            pledgedInputs.emplace(in, txid);
        }
    }

    return out;
}

CDposVoter::Output CDposVoter::applyTxVote(const CTxVote& vote)
{
    // for now, all the txs votings are done for a single round
    if (vote.nRound != 1 || !vote.choice.isStandardDecision()) {
        return misbehavingErr("masternode malformed tx vote");
    }

    if (vote.tip != tip && !world.allowArchiving(vote.tip)) {
        LogPrintf("dpos: %s: Ignoring too old transaction vote from block %s \n", __func__, vote.tip.GetHex());
        return {};
    }

    const TxId txid = vote.choice.subject;
    LogPrintf("dpos: %s: Received transaction vote for %s, from %s, decision=%d \n",
              __func__,
              txid.GetHex(),
              vote.voter.GetHex(),
              vote.choice.decision);
    auto& txVoting = v[vote.tip].txVotes[txid];

    // Check misbehaving or duplicating
    if (txVoting.count(vote.voter) > 0) {
        if (txVoting[vote.voter] != vote) { // shouldn't be possible, as tx vote cannot differ
            LogPrintf("dpos: %s: MISBEHAVING MASTERNODE! doublesign. tx voting, vote for %s, from %s \n",
                      __func__,
                      txid.GetHex(),
                      vote.voter.GetHex());
            return misbehavingErr("masternode tx doublesign misbehaving");
        }
        LogPrintf("dpos: %s: Ignoring duplicating transaction vote \n", __func__);
        return {};
    }
    if (v[vote.tip].mnTxVotes[vote.voter].size() >= maxTxVotesFromVoter) {
        LogPrintf("dpos: %s: MISBEHAVING MASTERNODE! too much votes. tx voting, vote for %s, from %s \n",
                  __func__,
                  txid.GetHex(),
                  vote.voter.GetHex());
        return misbehavingErr("masternode tx too much votes misbehaving");
    }

    txVoting.emplace(vote.voter, vote);
    v[vote.tip].mnTxVotes[vote.voter].push_back(vote);

    Output out;
    if (txs.count(txid) == 0) {
        // request the missing tx
        out.vTxReqs.push_back(txid);
        LogPrintf("dpos: %s: request the missing tx %s \n", __func__, txid.GetHex());
    } else {
        // update the index input -> txid
        const std::vector<COutPoint> inputs = getInputsOf(txs[txid]);
        for (const auto& in : inputs) {
            pledgedInputs.emplace(in, txid);
        }
    }

    return out + doRoundVoting();
}

CDposVoter::Output CDposVoter::applyRoundVote(const CRoundVote& vote)
{
    if (vote.nRound <= 0 || !vote.choice.isStandardDecision()) {
        return misbehavingErr("masternode malformed round vote");
    }

    if (vote.nRound <= 0) {
        LogPrintf("dpos: %s: MISBEHAVING MASTERNODE! malformed vote from %s \n",
                  __func__,
                  vote.voter.GetHex());
        return misbehavingErr("masternode malformed tx vote");
    }

    if (vote.tip != tip && !world.allowArchiving(vote.tip)) {
        LogPrintf("dpos: %s: Ignoring too old round vote from block %s \n", __func__, vote.tip.GetHex());
        return {};
    }

    LogPrintf("dpos: %s: Received round vote for %s, from %s, round %d \n",
              __func__,
              vote.choice.subject.GetHex(),
              vote.voter.GetHex(),
              vote.nRound);
    auto& roundVoting = v[vote.tip].roundVotes[vote.nRound];

    // Check misbehaving or duplicating
    if (roundVoting.count(vote.voter) > 0) {
        if (roundVoting[vote.voter] != vote) { // shouldn't be possible, as round vote cannot differ
            LogPrintf("dpos: %s: MISBEHAVING MASTERNODE! doublesign. round voting, vote for %s, from %s \n",
                      __func__,
                      vote.choice.subject.GetHex(),
                      vote.voter.GetHex());
            return misbehavingErr("masternode round doublesign misbehaving");
        }
        LogPrintf("dpos: %s: Ignoring duplicating Round vote \n", __func__);
        return {};
    }

    roundVoting.emplace(vote.voter, vote);

    Output out{};

    out += doRoundVoting();
    // check voting result after emplaced
    if (vote.choice.decision == CVoteChoice::Decision::YES) {
        out += tryToSubmitBlock(vote.choice.subject, vote.nRound);
    }

    return out;
}

CDposVoter::Output CDposVoter::requestMissingTxs()
{
    Output out{};
    uint32_t i = 0;
    for (BlockHash vot = tip; !vot.IsNull() && i < VOTING_MEMORY; i++, vot = world.getPrevBlock(vot)) {
        for (const auto& txVoting_p : v[vot].txVotes) {
            if (!txVoting_p.second.empty() && txs.count(txVoting_p.first) == 0) {
                out.vTxReqs.push_back(txVoting_p.first);
            }
        }
    }

    if (!out.vTxReqs.empty())
        LogPrintf("dpos: %s: %d \n", __func__, out.vTxReqs.size());

    return out;
}

CDposVoter::Output CDposVoter::ensurePledgeItemsNotMissing(const std::string& methodName, CDposVoter::MyTxsPledge& pledge) const {
    Output out{};
    if (!pledge.missingTxs.empty() || !pledge.missingViceBlocks.empty()) {
        // forbid voting if one of approved-by-me txs or vblocks is missing.
        // It means that I can't check that a tx doesn't interfere with already approved by me, or with a vice-block approved by me.
        // Without this condition, it's possible to do doublesign by accident.
        std::copy(pledge.missingTxs.begin(),
                  pledge.missingTxs.end(),
                  std::back_inserter(out.vTxReqs)); // request missing txs
        std::copy(pledge.missingViceBlocks.begin(),
                  pledge.missingViceBlocks.end(),
                  std::back_inserter(out.vViceBlockReqs)); // request missing vice-blocks
        LogPrintf("dpos: Can't do %s because %d of approved-by-me txs (one of them is %s). "
                  "%d of approved-by-me vice-blocks are missing (one of them is %s). "
                  "Txs/Vice-blocks are requested. \n",
                  methodName,
                  pledge.missingTxs.size(),
                  pledge.missingTxs.empty() ? std::string{"none"} : pledge.missingTxs.begin()->GetHex(),
                  pledge.missingViceBlocks.size(),
                  pledge.missingViceBlocks.empty() ? std::string{"none"} : pledge.missingViceBlocks.begin()->GetHex());
    }
    return out;
}

boost::optional<CRoundVote> CDposVoter::voteForViceBlock(const CBlock& viceBlock, CDposVoter::MyTxsPledge& pledge) const
{
    if (!amIvoter) {
        return {};
    }

    // vote for a vice-block
    boost::optional<CRoundVote> vote{};
    // check that this vice-block:
    // 1. round wasn't voted before
    if (wasVotedByMe_round(tip, viceBlock.nRound)) {
        LogPrintf("dpos: %s: skipping vice block %s at round %d, because this round was already voted by me \n", __func__, viceBlock.GetHash().GetHex(), viceBlock.nRound);
        return {};
    }

    // 2. may be connected
    if (!world.validateBlock(viceBlock, false)) {
        LogPrintf("dpos: %s: skipping vice block %s at round %d, because it cannot be connected \n", __func__, viceBlock.GetHash().GetHex(), viceBlock.nRound);
        return {};
    }

    // 3. doesn't interfere with my pledges
    std::set<TxId> viceBlockTxsSet;
    for (const auto& tx : viceBlock.vtx) {
        const std::vector<COutPoint> inputs = getInputsOf(tx);
        for (const auto& in : inputs) {
            if (pledge.assignedInputs.count(in) > 0 && pledge.assignedInputs[in] != tx.GetHash()) {  // this input is already assigned to another tx
                LogPrintf("dpos: %s: skipping vice block %s at round %d, because it assigns input %s to tx %s, but I promised it to tx %s \n", __func__, viceBlock.GetHash().GetHex(), viceBlock.nRound, in.ToString(), tx.GetHash().GetHex(), pledge.assignedInputs[in].GetHex());
                return {};
            }
        }
        viceBlockTxsSet.emplace(tx.GetHash());
    }

    // 4. does contain all the committed instant txs from prev. votings
    uint32_t i = 0;
    for (BlockHash vot = tip; !vot.IsNull() && i < VOTING_MEMORY; i++, vot = world.getPrevBlock(vot)) {
        if (i == 0)
            continue; // vice-block may not contain all the current committed txs, but must contain all the prev. committed txs
        const auto committedTxs = listCommittedTxs(vot);
        assert(committedTxs.missing.empty()); // we've already checked it
        for (const auto& tx : committedTxs.txs) {
            if (!world.validateTx(tx)) { // if it's invalid, it basically means that it was already included into a connected block
                continue;
            }
            if (viceBlockTxsSet.count(tx.GetHash()) == 0) {
                LogPrintf("dpos: %s: skipping vice block %s at round %d, because it doesn't contain committed (and not yet included) instant tx %s from prev. voting \n", __func__, viceBlock.GetHash().GetHex(), viceBlock.nRound, tx.GetHash().GetHex());
                return {};
            }
        }
    }

    { // vote
        CRoundVote newVote{};
        newVote.voter = me;
        newVote.choice = {viceBlock.GetHash(), CVoteChoice::Decision::YES};
        newVote.nRound = viceBlock.nRound;
        newVote.tip = tip;
        vote = {newVote};

        LogPrintf("dpos: %s: Vote for vice block %s at round %d \n", __func__, viceBlock.GetHash().GetHex(), viceBlock.nRound);
    }

    return vote;
}

CDposVoter::Output CDposVoter::doRoundVoting()
{
    if (!amIvoter) {
        return {};
    }

    auto pledge = myTxsPledge();
    const Output reqs = ensurePledgeItemsNotMissing("round voting", pledge);
    if (!reqs.empty()) {
        return reqs;
    }

    Output out{};

    struct BlockVotes {
        BlockVotes(size_t pro, arith_uint256 hash) {
            this->pro = pro;
            this->hash = hash;
        }
        size_t pro;
        arith_uint256 hash;
    };

    std::vector<BlockVotes> sortedViceBlocks{};

    // fill sortedViceBlocks
    for (const auto& viceBlock_p : v[tip].viceBlocks) {
        auto stats = calcRoundVotingStats(tip, viceBlock_p.second.nRound);
        sortedViceBlocks.emplace_back(stats.pro[viceBlock_p.first], UintToArith256(viceBlock_p.first));
    }

    // sort the vice-blocks by number of votes, vice-block Hash (decreasing)
    std::sort(sortedViceBlocks.begin(), sortedViceBlocks.end(), [](const BlockVotes& l, const BlockVotes& r) {
        if (l.pro == r.pro)
            return l.hash < r.hash;
        return l.pro < r.pro;
    });

    // vote for a vice-block
    // committed list may be not full, which is fine
    boost::optional<CRoundVote> vote{};
    for (auto&& viceBlockVotes : sortedViceBlocks) {
        vote = voteForViceBlock(v[tip].viceBlocks[ArithToUint256(viceBlockVotes.hash)], pledge);
        if (vote) {
            break;
        }
    }

    if (vote) {
        out.vRoundVotes.push_back(*vote);
        out += applyRoundVote(*vote);
    } else {
        LogPrintf("dpos: %s: Suitable vice block wasn't found, candidates=%d \n", __func__, sortedViceBlocks.size());
    }

    return out;
}

CDposVoter::Output CDposVoter::voteForTx(const CTransaction& tx)
{
    if (!amIvoter) {
        return {};
    }

    if (v[tip].mnTxVotes[me].size() >= maxTxVotesFromVoter / 2) {
        LogPrintf("dpos: %s: I'm exhausted, too much votes from me (it's ok, just number of txs is above limit) \n", __func__);
        return {};
    }

    TxId txid = tx.GetHash();
    const Round nRound = 1;

    if (wasVotedByMe_tx(txid, tip, nRound)) {
        LogPrintf("dpos: %s: Tx %s was already voted by me \n", __func__, txid.GetHex());
        return {};
    }

    auto pledge = myTxsPledge();
    const Output reqs = ensurePledgeItemsNotMissing("tx voting", pledge);
    if (!reqs.empty()) {
        return reqs;
    }

    Output out{};

    // check that this tx:
    // 1. may be included into a block
    if (!world.validateTx(tx))
        return {};
    // 2. doesn't interfere with instant txs I approved
    const std::set<COutPoint> txInputs = getInputsOf_set(tx);
    for (const auto& in : txInputs) {
        if (pledge.assignedInputs.count(in) > 0 && pledge.assignedInputs[in] != tx.GetHash()) {  // this input is already assigned to another tx
            LogPrintf("dpos: %s: skipping tx %s, because it assigns input %s, but I promised it to tx %s \n", __func__, tx.GetHash().GetHex(), in.ToString(), pledge.assignedInputs[in].GetHex());
            return {};
        }
    }
    // 3. doesn't interfere with vice-blocks I approved
    for (const auto& in : txInputs) {
        if (pledge.vblockAssignedInputs.count(in) == 1 && pledge.vblockAssignedInputs.find(in)->second == tx.GetHash()) {
            continue; // assigned to this tx
        } else if (pledge.vblockAssignedInputs.count(in) > 0) {  // this input is already assigned to another tx, in a vice-block I approved
            LogPrintf("dpos: %s: skipping tx %s, because it assigns input %s, but I promised it to another tx in a vice-block \n", __func__, tx.GetHash().GetHex(), in.ToString());
            return {};
        }
    }
    // 4. doesn't interfere with committed instant txs from prev. votings. It's not necessary because of step 2, but nice to avoid useless votes
    size_t txsSize = ::GetSerializeSize(tx, SER_NETWORK, tx.nVersion) + pledge.votedTxsSerializeSize;
    uint32_t i = 0;
    for (BlockHash vot = tip; !vot.IsNull() && i < VOTING_MEMORY; i++, vot = world.getPrevBlock(vot)) {
        const auto committedTxs = listCommittedTxs(vot);
        assert(committedTxs.missing.empty()); // we've already checked it

        for (const auto& cTx : committedTxs.txs) {
            const std::vector<COutPoint> cTxInputs = getInputsOf(cTx);
            if (cTx.GetHash() == txid)
                continue; // the same tx we vote for
            for (const auto& in : cTxInputs) {
                if (txInputs.count(in) > 0 ) {
                    LogPrintf("dpos: %s: skipping tx %s, because it interferes with the committed tx %s \n", __func__, tx.GetHash().GetHex(), cTx.GetHash().GetHex());
                    return {};
                }
            }
        }

        txsSize += ::GetSerializeSize(committedTxs.txs, SER_NETWORK, tx.nVersion); // yes, some txs are counted twice
    }
    // 5. doesn't exceed instant txs section size limit. IMPORTANT: we don't check MAX_INST_SECTION_SIGOPS because all the inputs are guaranteed to be P2PKH
    if (txsSize > MAX_INST_SECTION_SIZE / VOTING_MEMORY) {
        LogPrintf("dpos: %s: skipping tx %s, because it there's the size of instant txs is above limit \n", __func__, tx.GetHash().GetHex());
        return {};
    }

    {
        LogPrintf("dpos: %s: Vote for tx %s \n", __func__, txid.GetHex());
        CTxVote newVote{};
        newVote.voter = me;
        newVote.nRound = nRound;
        newVote.tip = tip;
        newVote.choice = CVoteChoice{txid, static_cast<int8_t>(CVoteChoice::Decision::YES)};
        out.vTxVotes.push_back(newVote);

        out += applyTxVote(newVote);
    }

    return out;
}

CDposVoter::Output CDposVoter::tryToSubmitBlock(BlockHash viceBlockId, Round nRound)
{
    Output out{};
    auto stats = calcRoundVotingStats(tip, nRound);

    if (stats.pro[viceBlockId] >= minQuorum) {
        if (v[tip].viceBlocks.count(viceBlockId) == 0) {
            return out;
        }
        const auto& viceBlock = v[tip].viceBlocks[viceBlockId];
        assert(viceBlock.nRound == nRound);

        if (viceBlock.hashPrevBlock != tip)
            return out;

        LogPrintf("dpos: %s: Submit block, num of votes = %d, minQuorum = %d \n",
                  __func__,
                  stats.pro[viceBlockId],
                  minQuorum);
        CBlockToSubmit blockToSubmit;
        blockToSubmit.block = viceBlock;

        const auto& votedBy_m = v[tip].roundVotes[nRound];
        for (const auto& vote_p : votedBy_m) {
            if (vote_p.second.choice.decision == CVoteChoice::Decision::YES)
                blockToSubmit.vApprovedBy.push_back(vote_p.second.voter);
        }

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
    LogPrintf("dpos: %s \n", __func__);
    for (const auto& tx_p : txs) {
        out += voteForTx(tx_p.second);
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

Round CDposVoter::getLowestNotOccupiedRound() const
{
    const size_t maxToCheck = 10000;
    for (Round i = 1; i < maxToCheck; i++) {
        const auto stats = calcRoundVotingStats(tip, i);
        if (stats.totus() <= (numOfVoters - minQuorum))
            return i;
    }
    return maxToCheck; // shouldn't be reachable
}

CDposVoter::CommittedTxs CDposVoter::listCommittedTxs(BlockHash vot) const
{
    CommittedTxs res{};
    if (v.count(vot) == 0 || v[vot].txVotes.empty())
        return res; // no votes at all

    for (const auto& txVoting_p : v[vot].txVotes) {
        const TxId txid = txVoting_p.first;
        if (isCommittedTx(txid, vot, 1)) {
            if (txs.count(txid) == 0)
                res.missing.emplace(txid);
            else
                res.txs.push_back(txs[txid]);
        }
    }

    return res;
}

bool CDposVoter::isCommittedTx(const TxId& txid, BlockHash vot, Round nRound) const
{
    const auto stats = calcTxVotingStats(txid, vot, nRound);

    return stats.pro >= minQuorum;
}

bool CDposVoter::isTxApprovedByMe(const TxId& txid, BlockHash vot, Round nRound) const
{
    if (v.count(vot) == 0 || v[vot].txVotes.count(txid) == 0)
        return false; // no votes at all for this tx

    if (v[vot].txVotes[txid].count(me) == 0)
        return false;
    const auto& myVote = v[vot].txVotes[txid][me];
    return myVote.choice.decision == CVoteChoice::Decision::YES;

}

CDposVoter::Output CDposVoter::misbehavingErr(const std::string& msg) const
{
    Output out;
    out.vErrors.push_back(msg);
    return out;
}

bool CDposVoter::wasVotedByMe_tx(TxId txid, BlockHash vot, Round nRound) const
{
    return isTxApprovedByMe(txid, vot, nRound); // there's only one type of decision, so voted == approved
}

bool CDposVoter::wasVotedByMe_round(BlockHash vot, Round nRound) const
{
    return v.count(vot) > 0 && v[vot].roundVotes.count(nRound) > 0 && v[vot].roundVotes[nRound].count(me) > 0;
}

CDposVoter::MyTxsPledge CDposVoter::myTxsPledge() const
{
    MyTxsPledge res{};
    // fill assignedInputs for last VOTING_MEMORY votings
    uint32_t i = 0;
    for (BlockHash vot = tip; !vot.IsNull() && i < VOTING_MEMORY; i++, vot = world.getPrevBlock(vot)) {
        myTxsPledgeForBlock(res, vot);
    }

    // fill vblockAssignedInputs for current voting
    for (auto&& roundVoting_p : v[tip].roundVotes) {
        if (roundVoting_p.second.count(me) == 0)
            continue;
        const auto& myVote = roundVoting_p.second[me];
        if (myVote.choice.decision != CVoteChoice::Decision::YES)
            continue;
        const BlockHash viceBlockId = myVote.choice.subject;

        if (v[tip].viceBlocks.count(viceBlockId) == 0) {
            // can happen after reindex, if we didn't download all the vice-blocks
            res.missingViceBlocks.emplace(viceBlockId);
        } else {
            const CBlock& viceBlock = v[tip].viceBlocks[viceBlockId];
            for (const auto& tx : viceBlock.vtx) {
                const std::vector<COutPoint> inputs = getInputsOf(tx);
                for (const auto& in : inputs) {
                    res.vblockAssignedInputs.emplace(in, tx.GetHash());
                }
            }
        }
    }

    return res;
}

void CDposVoter::myTxsPledgeForBlock(CDposVoter::MyTxsPledge& res, BlockHash vot) const
{
    if (v.count(vot) == 0)
        return;

    // fill assignedInputs
    for (auto&& txVoting_p : v[vot].txVotes) {
        const TxId txid = txVoting_p.first;

        if (isTxApprovedByMe(txid, vot, 1) && !checkTxNotCommittable(txid, vot)) {
            if (txs.count(txid) == 0) {
                // can happen after reindex, if we didn't download all the txs
                res.missingTxs.emplace(txid);
            } else {
                const CTransaction& tx = txs[txid];
                const std::vector<COutPoint> inputs = getInputsOf(tx);
                for (const auto& in : inputs) {
                    res.assignedInputs.emplace(in, tx.GetHash());
                }
                res.votedTxsSerializeSize += ::GetSerializeSize(tx, SER_NETWORK, tx.nVersion);
            }
        }
    }
}

CTxVotingDistribution CDposVoter::calcTxVotingStats(TxId txid, BlockHash vot, Round nRound) const
{
    CTxVotingDistribution stats{};

    if (v.count(vot) == 0 || v[vot].txVotes.count(txid) == 0)
        return stats;
    stats.pro = v[vot].txVotes[txid].size();

    return stats;
}

CRoundVotingDistribution CDposVoter::calcRoundVotingStats(BlockHash vot, Round nRound) const
{
    CRoundVotingDistribution stats{};
    // don't insert empty element if empty
    if (v.count(vot) == 0 || v[vot].roundVotes.count(nRound) == 0)
        return stats;

    for (const auto& vote_p : v[vot].roundVotes[nRound]) {
        const auto& vote = vote_p.second;
        // Do these sanity checks only here, no need to copy-paste them
        assert(vote.nRound == nRound);
        assert(vote.tip == tip);
        assert(vote.choice.decision == CVoteChoice::Decision::YES);

        switch (vote.choice.decision) {
        case CVoteChoice::Decision::YES:
            stats.pro[vote.choice.subject]++;
            break;
        default:
            assert(false);
            break;
        }
    }

    return stats;
}

bool CDposVoter::txHasAnyVote(TxId txid, BlockHash vot) const
{
    bool voteFound = false;

    uint32_t i = 0;
    for (BlockHash vot = tip; !vot.IsNull() && i < VOTING_MEMORY; i++, vot = world.getPrevBlock(vot)) {
        if (v.count(vot) == 0)
            continue;
        if (v[vot].txVotes.count(txid) > 0) {
            voteFound = true;
            break;
        }
    }
    return voteFound;
}

bool CDposVoter::wasTxLost(TxId txid, BlockHash vot) const
{
    if (txs.count(txid) > 0) // known
        return false;
    return txHasAnyVote(txid, vot);
}

bool CDposVoter::checkTxNotCommittable(TxId txid, BlockHash vot) const
{
    if (isCommittedTx(txid, vot, 1))
        return false;

    if (txs.count(txid) == 0)
        return false; // assume worse if tx is missing

    const CTransaction& tx = txs[txid];
    const std::vector<COutPoint> inputs = getInputsOf(tx);
    for (const auto& in : inputs) {
        // iterate over all the voted txs which use tha same inputs
        const auto& collisions_p = this->pledgedInputs.equal_range(in);
        for (auto colliTx_it = collisions_p.first; colliTx_it != collisions_p.second; colliTx_it++) {
            if (colliTx_it->second != txid && isCommittedTx(colliTx_it->second, vot, 1))
                return true;
        }
    }

    return false;
}

bool CDposVoter::verifyVotingState() const
{
    std::set<uint256> txVotes;
    std::set<uint256> mnTxVotes;

    // don't insert empty element if empty
    if (v.count(tip) == 0)
        return true;

    for (const auto& txVoting_p : v[tip].txVotes) {
        for (const auto& vote_p : txVoting_p.second) {
            if (!txVotes.emplace(vote_p.second.GetHash()).second) {
                return false; // no duplicates possible
            }
        }
    }
    for (const auto& mnVotes_p : v[tip].mnTxVotes) {
        for (const auto& vote : mnVotes_p.second) {
            if (!mnTxVotes.emplace(vote.GetHash()).second) {
                return false; // no duplicates possible
            }
        }
    }

    return txVotes == mnTxVotes;
}

} // namespace dpos
