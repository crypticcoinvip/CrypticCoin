// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "dpos_controller.h"
#include "dpos_voter.h"
#include "dpos_validator.h"
#include "../timedata.h"
#include "../chainparams.h"
#include "../init.h"
#include "../key.h"
#include "../main.h"
#include "../net.h"
#include "../txdb.h"
#include "../wallet/wallet.h"
#include "../snark/libsnark/common/utils.hpp"
#include <mutex>
#include <future>
#include <boost/thread.hpp>

namespace dpos
{
CDposController* dposControllerInstance_{nullptr};

BlockHash getTipHash()
{
    LOCK(cs_main);
    assert(chainActive.Tip() != nullptr);
    return chainActive.Tip()->GetBlockHash();
}

std::size_t getActiveMasternodeCount()
{
    LOCK(cs_main);
    return pmasternodesview->GetActiveMasternodes().size();
}

std::size_t getTeamSizeCount(int height)
{
    LOCK(cs_main);
    return pmasternodesview->ReadDposTeam(height).size();
}

std::vector<CNode*> getNodes()
{
    LOCK(cs_vNodes);
    return {vNodes.begin(), vNodes.end()};
}

CKey getMasternodeKey()
{
    CKey rv{};
#ifdef ENABLE_WALLET
    LOCK(cs_main);
    const auto mnId{pmasternodesview->AmIActiveOperator()};
    if (mnId == boost::none ||
        !pwalletMain->GetKey(mnId.get().operatorAuthAddress, rv))
    {
        rv = CKey{};
    }
#endif
    return rv;
}

template<typename T>
void relayEntity(const T& entity, int type)
{
    // Expire old relay messages
    LOCK(cs_mapRelay);
    while (!vRelayExpiration.empty() &&
           vRelayExpiration.front().first < GetTime())
    {
        mapRelay.erase(vRelayExpiration.front().second);
        vRelayExpiration.pop_front();
    }

    // Save original serialized message so newer versions are preserved
    CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};
    const CInv inv{type, entity.GetHash()};

    ss.reserve(1024);
    ss << entity;

    mapRelay.insert(std::make_pair(inv, ss));
    vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
    BroadcastInventory(inv);
}


template<typename T, typename StoreMethod>
void storeEntity(const T& entity, StoreMethod storeMethod, const BlockHash& tipHash)
{
    LOCK(cs_main);
    (pdposdb->*storeMethod)(tipHash, entity, nullptr);
}

CDposController& CDposController::getInstance()
{
    using std::placeholders::_1;
    using std::placeholders::_2;
    using std::placeholders::_3;

    if (dposControllerInstance_ == nullptr) {
        LOCK(cs_main);
        if (dposControllerInstance_ == nullptr) {
            dposControllerInstance_ = new CDposController{};
            std::make_shared<Validator>().swap(dposControllerInstance_->validator);
            Validator* validator{dposControllerInstance_->validator.get()};
            CDposVoter::Callbacks callbacks{};
            callbacks.validateTx = std::bind(&Validator::validateTx, validator, _1);
            callbacks.validateTxs = std::bind(&Validator::validateTxs, validator, _1);
            callbacks.validateBlock = std::bind(&Validator::validateBlock, validator, _1, _2, _3);
            callbacks.allowArchiving = std::bind(&Validator::allowArchiving, validator, _1);
            std::make_shared<CDposVoter>(callbacks).swap(dposControllerInstance_->voter);
        }
    }
    assert(dposControllerInstance_ != nullptr);
    return *dposControllerInstance_;
}

void CDposController::runEventLoop()
{
    int64_t lastTipChangeT{GetTimeMillis()};
    BlockHash lastTip{};

    std::pair<BlockHash, Round> lastRound{};
    int64_t lastRoundChangeT{GetTimeMillis()};

    int64_t lastSyncT{GetTimeMillis() - 1000000};

    int64_t initialBlocksDownloadPassedT{0};

    CDposController* self{getController()};
    const Consensus::Params& params{Params().GetConsensus()};

    self->onChainTipUpdated(getTipHash());

    while (true) {
        boost::this_thread::interruption_point();

        const BlockHash tip{getTipHash()};
        if (self->isEnabled(tip)) {
            const auto now{GetTimeMillis()};
            { // initialVotesDownload logic. Don't vote if not passed {nDelayIBD} seconds since blocks are downloaded
                if (initialBlocksDownloadPassedT == 0 && !IsInitialBlockDownload()) {
                    initialBlocksDownloadPassedT = now;
                }
                if (initialBlocksDownloadPassedT == 0 && (now - lastTipChangeT) > 2 * 60 * 1000) {
                    initialBlocksDownloadPassedT = now;
                }

                if (self->initialVotesDownload && initialBlocksDownloadPassedT > 0 && ((now - initialBlocksDownloadPassedT) > params.dpos.nDelayIBD * 1000)) {
                    self->initialVotesDownload = false;
                    self->onChainTipUpdated(tip);
                }
            }

            { // maintenance under cs_main
                LOCK(cs_main);
                {
                    const std::pair<BlockHash, Round> currentRound{self->voter->getTip(), self->getCurrentVotingRound()};

                    if ((now - lastRoundChangeT) > (params.dpos.nRoundTooLong * 1000) && lastRound == currentRound) {
                        self->handleVoterOutput(self->voter->onRoundTooLong());
                    }

                    // track changes of tip and round
                    if (lastRound != currentRound)
                        lastRoundChangeT = now;
                    lastRound = currentRound;
                    if (lastTip != tip)
                        lastTipChangeT = now;
                    lastTip = tip;
                }

                // try to find missing txs in mempool
                for (auto it = self->vTxReqs.begin(); it != self->vTxReqs.end();) {
                    CTransaction tx;
                    if (mempool.lookup(it->hash, tx)) {
                        self->handleVoterOutput(self->voter->applyTx(tx));
                        it = self->vTxReqs.erase(it);
                    } else {
                        it++;
                    }
                }

                // periodically rm waste data from old blocks
                self->cleanUpDb();
            }

            { // p2p syncing requests
                int64_t syncPeriod = params.dpos.nPollingPeriod * 1000;
                if (self->initialVotesDownload)
                    syncPeriod /= 10; // poll 10 times more often when initialVotesDownload
                if (syncPeriod < 1000) // not more often than once in 1s
                    syncPeriod = 1000;

                const auto nodes = getNodes(); // protected by cs inside
                if (!nodes.empty() && (now - lastSyncT) > syncPeriod) {
                    lastSyncT = now;

                    std::vector<CInv> txReqsToSend;
                    {
                        LOCK(cs_main);
                        txReqsToSend.insert(txReqsToSend.end(), self->vTxReqs.begin(), self->vTxReqs.end());
                    }
                    for (auto&& node : nodes) {
                        node->PushMessage("getvblocks", tip);
                        node->PushMessage("getrvotes", tip);
                        node->PushMessage("gettxvotes", tip, self->getTxsFilter());
                        if (!txReqsToSend.empty())
                            node->PushMessage("getdata", txReqsToSend);
                    }
                }
            }
        }

        MilliSleep(1000);
    }
}

bool CDposController::isEnabled(int tipHeight) const
{
    const Consensus::Params& params{Params().GetConsensus()};
    if (tipHeight < 0) {
        LOCK(cs_main);
        tipHeight = chainActive.Height();
    }

    // Disable dPoS if mns are offline
    const bool fBigGapBetweenBlocks = (GetAdjustedTime() - chainActive.Tip()->GetBlockTime()) > params.dpos.nMaxTimeBetweenBlocks;

    const std::size_t nCurrentTeamSize{getTeamSizeCount(tipHeight)};
    return NetworkUpgradeActive(tipHeight, params, Consensus::UPGRADE_SAPLING) &&
           nCurrentTeamSize == params.dpos.nTeamSize &&
           !fBigGapBetweenBlocks;
}

bool CDposController::isEnabled(const BlockHash& tipHash) const
{
    int height{-1};

    if (!tipHash.IsNull()) {
        LOCK(cs_main);
        height = Validator::computeBlockHeight(tipHash);
    }

    return isEnabled(height);
}

CValidationInterface* CDposController::getValidator()
{
    return this->validator.get();
}

void CDposController::loadDB()
{
    assert(pdposdb != nullptr);
    assert(!this->voter->checkAmIVoter());
    assert(this->initialVotesDownload);

    voter->minQuorum = Params().GetConsensus().dpos.nMinQuorum;
    voter->numOfVoters = Params().GetConsensus().dpos.nTeamSize;
    voter->maxNotVotedTxsToKeep = Params().GetConsensus().dpos.nMaxNotVotedTxsToKeep;
    voter->maxTxVotesFromVoter = Params().GetConsensus().dpos.nMaxTxVotesFromVoter;

    bool success = false;

    success = pdposdb->LoadViceBlocks([this](const BlockHash& blockHash, const CBlock& block) {
        if (block.GetHash() != blockHash)
            throw std::runtime_error("dPoS database is corrupted (reading vice-blocks)! Please restart with -reindex to recover.");
        this->voter->v[block.hashPrevBlock].viceBlocks.emplace(block.GetHash(), block);
    });
    if (!success)
        throw std::runtime_error("dPoS database is corrupted (reading vice-blocks)! Please restart with -reindex to recover.");

    success = pdposdb->LoadRoundVotes([this](const uint256& voteHash, const CRoundVote_p2p& vote) {
        if (vote.GetHash() != voteHash)
            throw std::runtime_error("dPoS database is corrupted (reading round votes)! Please restart with -reindex to recover.");
        const auto mnId{authenticateMsg(vote)};
        if (mnId != boost::none) {
            CRoundVote roundVote{};
            roundVote.tip = vote.tip;
            roundVote.voter = mnId.get();
            roundVote.nRound = vote.nRound;
            roundVote.choice = vote.choice;

            this->receivedRoundVotes.emplace(vote.GetHash(), vote);
            this->voter->v[vote.tip].roundVotes[roundVote.nRound].emplace(roundVote.voter, roundVote);
        }
    });
    if (!success)
        throw std::runtime_error("dPoS database is corrupted (reading vice-blocks)! Please restart with -reindex to recover.");

    success = pdposdb->LoadTxVotes([this](const uint256& voteHash, const CTxVote_p2p& vote) {
        if (vote.GetHash() != voteHash)
            throw std::runtime_error("dPoS database is corrupted (reading tx votes)! Please restart with -reindex to recover.");
        const auto mnId{authenticateMsg(vote)};
        if (mnId != boost::none) {
            for (const auto& choice : vote.choices) {
                CTxVote txVote{};
                txVote.tip = vote.tip;
                txVote.voter = mnId.get();
                txVote.nRound = vote.nRound;
                txVote.choice = choice;

                this->voter->v[vote.tip].txVotes[txVote.nRound][choice.subject].emplace(txVote.voter, txVote);
                this->voter->v[vote.tip].mnTxVotes[txVote.voter].push_back(txVote);
            }
            this->receivedTxVotes.emplace(vote.GetHash(), vote);
        }
    });
    if (!success)
        throw std::runtime_error("dPoS database is corrupted (reading tx votes)! Please restart with -reindex to recover.");

    if (!this->voter->verifyVotingState())
        throw std::runtime_error("dPoS database is corrupted (voting state verification failed)! Please restart with -reindex to recover.");
}

void CDposController::onChainTipUpdated(const BlockHash& tip)
{
    if (isEnabled(tip)) {
        const auto mnId{findMyMasternodeId()};
        LOCK(cs_main);

        if (!initialVotesDownload && mnId != boost::none && !this->voter->checkAmIVoter()) {
            LogPrintf("dpos: %s: I became a team member, enabling voter for me (I'm %s)\n", __func__, mnId.get().GetHex());
            this->voter->setVoting(true, mnId.get());
        } else if (mnId == boost::none && this->voter->checkAmIVoter()) {
            LogPrintf("dpos: %s: Disabling voter, I'm not a team member for now \n", __func__);
            this->voter->setVoting(false, CMasternode::ID{});
        }

        this->voter->updateTip(tip);
        handleVoterOutput(this->voter->requestMissingTxs() + this->voter->doTxsVoting() + this->voter->doRoundVoting());
    }
}

Round CDposController::getCurrentVotingRound() const
{
    if (isEnabled()) {
        LOCK(cs_main);
        return voter->getCurrentRound();
    }
    return 0;
}

void CDposController::proceedViceBlock(const CBlock& viceBlock)
{
    bool success = false;
    LOCK(cs_main);
    {
        if (!findViceBlock(viceBlock.GetHash())) {
            const CDposVoterOutput out{voter->applyViceBlock(viceBlock)};

            if (handleVoterOutput(out)) {
                success = true;
            }
        }
    }
    if (success) {
        storeEntity(viceBlock, &CDposDB::WriteViceBlock, viceBlock.GetHash());
        relayEntity(viceBlock, MSG_VICE_BLOCK);
    }
}

void CDposController::proceedTransaction(const CTransaction& tx)
{
    LOCK(cs_main);
    vTxReqs.erase(CInv{MSG_TX, tx.GetHash()});
    handleVoterOutput(voter->applyTx(tx));
}

void CDposController::proceedRoundVote(const CRoundVote_p2p& vote)
{
    bool success = false;
    LOCK(cs_main);
    {
        if (!findRoundVote(vote.GetHash())) {

            this->receivedRoundVotes.emplace(vote.GetHash(), vote); // emplace before to be able to find signature to submit block
            if (acceptRoundVote(vote)) {
                success = true;
            } else {
                this->receivedRoundVotes.erase(vote.GetHash());
            }
        }
    }
    if (success) {
        storeEntity(vote, &CDposDB::WriteRoundVote, vote.GetHash());
        relayEntity(vote, MSG_ROUND_VOTE);
    }
}

void CDposController::proceedTxVote(const CTxVote_p2p& vote)
{
    bool success = false;
    {
        LOCK(cs_main);
        if (!findTxVote(vote.GetHash())) {

            if (acceptTxVote(vote)) {
                success = true;
            }
        }
    }
    if (success) {
        this->receivedTxVotes.emplace(vote.GetHash(), vote);
        storeEntity(vote, &CDposDB::WriteTxVote, vote.GetHash());
        relayEntity(vote, MSG_TX_VOTE);
    }
}

bool CDposController::findViceBlock(const BlockHash& hash, CBlock* block) const
{
    AssertLockHeld(cs_main);

    for (const auto& pair : this->voter->v) {
        const auto it{pair.second.viceBlocks.find(hash)};
        if (it != pair.second.viceBlocks.end()) {
            if (block != nullptr) {
                *block = it->second;
            }
            return true;
        }
    }

    return false;
}

bool CDposController::findRoundVote(const BlockHash& hash, CRoundVote_p2p* vote) const
{
    AssertLockHeld(cs_main);
    const auto it{this->receivedRoundVotes.find(hash)};
    const auto rv{it != this->receivedRoundVotes.end()};

    if (rv && vote != nullptr) {
        *vote = it->second;
    }

    return rv;
}

bool CDposController::findTxVote(const BlockHash& hash, CTxVote_p2p* vote) const
{
    AssertLockHeld(cs_main);
    const auto it{this->receivedTxVotes.find(hash)};
    const auto rv{it != this->receivedTxVotes.end()};

    if (rv && vote != nullptr) {
        *vote = it->second;
    }

    return rv;
}

bool CDposController::findTx(const TxId& txid, CTransaction* tx) const
{
    AssertLockHeld(cs_main);
    const auto it{this->voter->txs.find(txid)};
    const auto rv{it != this->voter->txs.end()};

    if (rv && tx != nullptr) {
        *tx = it->second;
    }

    return rv;
}

std::vector<CBlock> CDposController::listViceBlocks() const
{
    std::vector<CBlock> rv{};
    LOCK(cs_main);

    for (const auto& pair1 : this->voter->v) {
        for (const auto& pair2 : pair1.second.viceBlocks) {
            rv.push_back(pair2.second);
        }
    }

    return rv;
}

std::vector<CRoundVote_p2p> CDposController::listRoundVotes() const
{
    std::vector<CRoundVote_p2p> rv{};
    LOCK(cs_main);

    rv.reserve(this->receivedRoundVotes.size());

    for (const auto& pair : this->receivedRoundVotes) {
        assert(pair.first == pair.second.GetHash());
        rv.emplace_back(pair.second);
    }

    return rv;
}

std::vector<CTxVote_p2p> CDposController::listTxVotes() const
{
    std::vector<CTxVote_p2p> rv{};
    LOCK(cs_main);

    rv.reserve(this->receivedTxVotes.size());

    for (const auto& pair : this->receivedTxVotes) {
        assert(pair.first == pair.second.GetHash());
        rv.emplace_back(pair.second);
    }

    return rv;
}

std::vector<CTransaction> CDposController::listCommittedTxs() const
{
    std::vector<CTransaction> rv{};
    std::map<TxIdSorted, CTransaction> txs{};

    {
        LOCK(cs_main);
        txs = this->voter->listCommittedTxs();
    }

    rv.reserve(txs.size());

    for (const auto& pair : txs) {
        rv.emplace_back(pair.second);
    }

    return rv;
}

bool CDposController::isCommittedTx(const TxId& txid) const
{
    LOCK(cs_main);
    return this->voter->isCommittedTx(txid);
}

CTxVotingDistribution CDposController::calcTxVotingStats(const TxId& txid) const
{
    LOCK(cs_main);
    return this->voter->calcTxVotingStats(txid, this->voter->getCurrentRound());
}

bool CDposController::isTxApprovedByMe(const TxId& txid) const
{
    LOCK(cs_main);
    return this->voter->isTxApprovedByMe(txid);
}

bool CDposController::handleVoterOutput(const CDposVoterOutput& out)
{
    AssertLockHeld(cs_main);
    if (!out.vErrors.empty()) {
        for (const auto& error : out.vErrors) {
            LogPrintf("dpos: %s: %s\n", __func__, error);
        }
        return false;
    }

    if (!out.empty()) {
        const CKey masternodeKey{getMasternodeKey()};

        if (masternodeKey.IsValid()) {
            // process before blockToSubmit, to be able to find signatures
            for (const auto& roundVote : out.vRoundVotes) {
                CRoundVote_p2p vote{};
                vote.tip = roundVote.tip;
                vote.nRound = roundVote.nRound;
                vote.choice = roundVote.choice;

                if (!masternodeKey.SignCompact(vote.GetSignatureHash(), vote.signature)) {
                    LogPrintf("dpos: %s: Can't sign round vote\n", __func__);
                } else {
                    this->receivedRoundVotes.emplace(vote.GetHash(), vote);
                    storeEntity(vote, &CDposDB::WriteRoundVote, vote.GetHash());
                    relayEntity(vote, MSG_ROUND_VOTE);
                }
            }
            for (const auto& txVote : out.vTxVotes) {
                CTxVote_p2p vote{};
                vote.tip = txVote.tip;
                vote.nRound = txVote.nRound;
                vote.choices.push_back(txVote.choice);

                if (!masternodeKey.SignCompact(vote.GetSignatureHash(), vote.signature)) {
                    LogPrintf("dpos: %s: Can't sign tx vote\n", __func__);
                } else {
                    this->receivedTxVotes.emplace(vote.GetHash(), vote);
                    storeEntity(vote, &CDposDB::WriteTxVote, vote.GetHash());
                    relayEntity(vote, MSG_TX_VOTE);
                }
            }
        }

        for (const TxId& txReq : out.vTxReqs) {
            if (this->vTxReqs.size() >= MAX_INV_SZ)
                break;
            this->vTxReqs.emplace(MSG_TX, txReq);
        }

        if (out.blockToSubmit != boost::none) {
            CValidationState state{};
            CBlockToSubmit blockToSubmit{out.blockToSubmit.get()};
            CBlock* pblock{&blockToSubmit.block};
            BlockHash blockHash = pblock->GetHash();

            for (const auto& votePair : this->receivedRoundVotes) {
                if (votePair.second.nRound == pblock->nRound &&
                    votePair.second.choice.decision == CVoteChoice::Decision::YES &&
                    votePair.second.choice.subject == blockHash &&
                    authenticateMsg(votePair.second) != boost::none)
                {
                    pblock->vSig.insert(pblock->vSig.end(),
                                        votePair.second.signature.begin(),
                                        votePair.second.signature.end());
                }
            }
            if ((pblock->vSig.size() / CPubKey::COMPACT_SIGNATURE_SIZE) < this->voter->minQuorum) {
                LogPrintf("dpos: %s: Can't submit block - missing signatures (%d < %d)\n",
                          __func__,
                          pblock->vSig.size() / CPubKey::COMPACT_SIGNATURE_SIZE,
                          this->voter->minQuorum);
            } else if (!ProcessNewBlock(state, nullptr, const_cast<CBlock*>(pblock), true, nullptr)) {
                LogPrintf("dpos: %s: Can't ProcessNewBlock\n", __func__);
            }
        }
    }

    return true;
}

bool CDposController::acceptRoundVote(const CRoundVote_p2p& vote)
{
    AssertLockHeld(cs_main);
    bool rv{true};
    const auto mnId{authenticateMsg(vote)};

    if (mnId == boost::none) {
        rv = false;
    } else {
        CRoundVote roundVote{};
        roundVote.tip = vote.tip;
        roundVote.voter = mnId.get();
        roundVote.nRound = vote.nRound;
        roundVote.choice = vote.choice;

        rv = handleVoterOutput(voter->applyRoundVote(roundVote));
    }

    return rv;
}

bool CDposController::acceptTxVote(const CTxVote_p2p& vote)
{
    if (vote.choices.size() != 1) {
        return false; // currently, accept only votes with 1 tx to avoid issues with partially accepted votes
    }

    AssertLockHeld(cs_main);
    bool rv{true};
    const auto mnId{authenticateMsg(vote)};

    if (mnId == boost::none) {
        rv = false;
    } else {
        CTxVote txVote{};
        txVote.tip = vote.tip;
        txVote.voter = mnId.get();
        txVote.nRound = vote.nRound;

        assert(vote.choices.size() == 1);
        for (const auto& choice : vote.choices) {
            txVote.choice = choice;

            if (!handleVoterOutput(voter->applyTxVote(txVote))) {
                rv = false;
            }
        }
    }

    return rv;
}

boost::optional<CMasternode::ID> CDposController::findMyMasternodeId()
{
    LOCK(cs_main);
    const auto mnIds = pmasternodesview->AmIActiveOperator();
    if (mnIds == boost::none) {
        return boost::none;
    }

    return getIdOfTeamMember(getTipHash(), mnIds->operatorAuthAddress);
}

boost::optional<CMasternode::ID> CDposController::getIdOfTeamMember(const BlockHash& blockHash, const CKeyID& operatorAuth)
{
    LOCK(cs_main);
    const int height{Validator::computeBlockHeight(blockHash, MIN_BLOCKS_TO_KEEP)};
    const CTeam team = pmasternodesview->ReadDposTeam(height);
    for (auto&& member : team)
    {
        if (member.second.second == operatorAuth)
            return {member.first};
    }
    if (team.empty()) {
        LogPrintf("dpos: Couldn't read dPoS team as it was already cleared \n");
    }

    return boost::none;
}

boost::optional<CMasternode::ID> CDposController::authenticateMsg(const CTxVote_p2p& vote)
{
    CPubKey pubKey{};
    if (!pubKey.RecoverCompact(vote.GetSignatureHash(), vote.signature))
    {
        return boost::none;
    }
    return getIdOfTeamMember(vote.tip, pubKey.GetID());
}

boost::optional<CMasternode::ID> CDposController::authenticateMsg(const CRoundVote_p2p& vote)
{
    CPubKey pubKey{};
    if (!pubKey.RecoverCompact(vote.GetSignatureHash(), vote.signature))
    {
        return boost::none;
    }
    return getIdOfTeamMember(vote.tip, pubKey.GetID());
}

void CDposController::cleanUpDb()
{
//    AssertLockHeld(cs_main);
//    const auto tipHeight{chainActive.Height()};

//    for (const auto& pair: this->receivedRoundVotes) {
//        if (tipHeight - computeBlockHeight(pair.second.tip, MIN_BLOCKS_TO_KEEP) > MIN_BLOCKS_TO_KEEP) {
//            this->receivedRoundVotes.erase(pair.first);
//            pdposdb->EraseRoundVote(pair.second.GetHash());
//        }
//    }
//    for (const auto& pair: this->receivedTxVotes) {
//        if (tipHeight - computeBlockHeight(pair.second.tip, MIN_BLOCKS_TO_KEEP) > MIN_BLOCKS_TO_KEEP) {
//            this->receivedRoundVotes.erase(pair.first);
//            pdposdb->EraseTxVote(pair.second.GetHash());
//        }
//    }

//    for (const auto& pair: this->voter->v) {
//        if (tipHeight - computeBlockHeight(pair.first, MIN_BLOCKS_TO_KEEP) > MIN_BLOCKS_TO_KEEP) {
//            pdposdb->EraseViceBlock(pair.second.GetHash());
//        }
//    }
}

std::vector<TxId> CDposController::getTxsFilter() const
{
    std::vector<TxId> rv{};
    return rv;
}

} //namespace dpos
