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
    if (chainActive.Tip() == nullptr)
        return BlockHash{};
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
void storeEntity(const T& entity, StoreMethod storeMethod, const uint256& key)
{
    LOCK(cs_main);
    (pdposdb->*storeMethod)(key, entity, nullptr);
}

CDposController& CDposController::getInstance()
{
    using std::placeholders::_1;
    using std::placeholders::_2;

    if (dposControllerInstance_ == nullptr) {
        LOCK(cs_main);
        if (dposControllerInstance_ == nullptr) {
            dposControllerInstance_ = new CDposController{};
            std::make_shared<Validator>().swap(dposControllerInstance_->validator);
            Validator* validator{dposControllerInstance_->validator.get()};
            CDposVoter::Callbacks callbacks{};
            callbacks.preValidateTx = std::bind(&Validator::preValidateTx, validator, _1, _2);
            callbacks.validateTx = std::bind(&Validator::validateTx, validator, _1);
            callbacks.validateBlock = std::bind(&Validator::validateBlock, validator, _1, _2);
            callbacks.allowArchiving = std::bind(&Validator::allowArchiving, validator, _1);
            callbacks.getPrevBlock = std::bind(&Validator::getPrevBlock, validator, _1);
            callbacks.getTime = []() {
                return GetTimeMillis();
            };
            std::make_shared<CDposVoter>(callbacks).swap(dposControllerInstance_->voter);
        }
    }
    assert(dposControllerInstance_ != nullptr);
    return *dposControllerInstance_;
}

void CDposController::runEventLoop()
{
    int64_t lastTipChangeT{GetTimeMillis()};

    int64_t lastSyncT{0};

    int64_t initialBlocksDownloadPassedT{0};

    CDposController* self{getController()};
    const Consensus::Params& params{Params().GetConsensus()};

    while(chainActive.Tip() == nullptr) {
        MilliSleep(100);
    }

    {
        LOCK(cs_main);
        self->onChainTipUpdated(getTipHash());
    }

    while (true) {
        boost::this_thread::interruption_point();

        const BlockHash tip{getTipHash()};
        const int64_t now{GetTimeMillis()};

        try {
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
                // try to find missing txs in mempool
                for (auto it = self->vReqs.begin(); it != self->vReqs.end();) {
                    CTransaction tx;
                    if (mempool.lookup(it->hash, tx)) {
                        CValidationState state;
                        self->handleVoterOutput(self->voter->applyTx(tx), state);
                        it = self->vReqs.erase(it);
                    } else {
                        it++;
                    }
                }

                if ((now - self->voter->lastRoundVotedTime) > params.dpos.nDelayBetweenRoundVotes * 1000) {
                    self->voter->resetRoundVotingTimer();
                }
            }

            { // p2p syncing requests
                int64_t syncPeriod = params.dpos.nPollingPeriod * 1000;
                if (self->initialVotesDownload)
                    syncPeriod /= 4; // poll 4 times more often when initialVotesDownload
                if (syncPeriod < 1000) // not more often than once in 1s
                    syncPeriod = 1000;

                {
                    std::vector<CInv> reqsToSend;
                    std::vector<BlockHash> interestedVotings;
                    {
                        LOCK(cs_main);
                        reqsToSend.insert(reqsToSend.end(), self->vReqs.begin(), self->vReqs.end());
                        for (int i = chainActive.Height(); i > 0 && i > (chainActive.Height() - (int) CDposVoter::GUARANTEES_MEMORY); i--)
                            interestedVotings.push_back(chainActive[i]->GetBlockHash());
                    }

                    { // don't lock cs_main here
                        auto nodes = CNodesShared::getSharedList(); // cs_vNodes inside constructor/desctructor
                        if (!nodes.empty()) {
                            auto& fullSyncNode = nodes[rand() % nodes.size()];
                            if (now - lastSyncT >
                                syncPeriod) { // send full sync req only to one node, only once within syncPeriod
                                for (auto&& v : interestedVotings) {
                                    fullSyncNode->PushMessage("getvblocks", v);
                                    fullSyncNode->PushMessage("getrvotes", v);
                                    fullSyncNode->PushMessage("gettxvotes", v, self->getTxsFilter());
                                }
                                lastSyncT = now;
                            }

                            for (auto&& node : nodes) { // send concrete requests to all the nodes every second
                                if (!reqsToSend.empty())
                                    node->PushMessage("getdata", reqsToSend);
                            }
                        }
                    }
                }
            }
        } catch (std::exception& e) {
            LogPrintf("%s: %s \n", __func__, e.what());
        } catch (...) {
            LogPrintf("%s: unknown exception \n", __func__);
        }

        MilliSleep(1000);
    }
}

bool CDposController::isEnabled(int64_t time, const CBlockIndex* pindexTip) const
{
    const Consensus::Params& params{Params().GetConsensus()};

    if (pindexTip == nullptr) {
        AssertLockHeld(cs_main);
        pindexTip = chainActive.Tip();
    }

    // Disable dPoS if mns are offline
    return NetworkUpgradeActive(pindexTip->nHeight, params, Consensus::UPGRADE_SAPLING) &&
           getTeamSizeCount(pindexTip->nHeight) == params.dpos.nTeamSize &&
           time < (pindexTip->GetBlockTime() + params.dpos.nMaxTimeBetweenBlocks(pindexTip->nHeight + 1));
}

bool CDposController::isEnabled(int64_t time, int tipHeight) const
{
    LOCK(cs_main);
    CBlockIndex* pindexTip{nullptr};
    if (tipHeight >= 0 && tipHeight <= chainActive.Height()) {
        pindexTip = chainActive[tipHeight];
    }
    return isEnabled(time, pindexTip);
}

bool CDposController::isEnabled(int64_t time, const BlockHash& tipHash) const
{
    CBlockIndex* pindexTip{nullptr};

    if (!tipHash.IsNull()) {
        LOCK(cs_main);
        if (mapBlockIndex.find(tipHash) == mapBlockIndex.end()) {
            return false;
        }
        pindexTip = mapBlockIndex[tipHash];
    }

    return isEnabled(time, pindexTip);
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
        this->voter->insertViceBlock(block);
    });
    if (!success)
        throw std::runtime_error("dPoS database is corrupted (reading vice-blocks)! Please restart with -reindex to recover.");

    success = pdposdb->LoadRoundVotes([this](const uint256& voteHash, const CRoundVote_p2p& vote) {
        if (vote.GetHash() != voteHash)
            throw std::runtime_error("dPoS database is corrupted (reading round votes)! Please restart with -reindex to recover.");
        CValidationState state;
        const auto mnId{authenticateMsg(vote, state)};
        if (mnId != boost::none) {
            CRoundVote roundVote{};
            roundVote.tip = vote.tip;
            roundVote.voter = mnId.get();
            roundVote.nRound = vote.nRound;
            roundVote.choice = vote.choice;

            this->receivedRoundVotes.emplace(vote.GetHash(), vote);
            this->voter->insertRoundVote(roundVote);
        }
    });
    if (!success)
        throw std::runtime_error("dPoS database is corrupted (reading vice-blocks)! Please restart with -reindex to recover.");

    success = pdposdb->LoadTxVotes([this](const uint256& voteHash, const CTxVote_p2p& vote) {
        if (vote.GetHash() != voteHash)
            throw std::runtime_error("dPoS database is corrupted (reading tx votes)! Please restart with -reindex to recover.");
        CValidationState state;
        const auto mnId{authenticateMsg(vote, state)};
        if (mnId != boost::none) {
            for (const auto& choice : vote.choices) {
                CTxVote txVote{};
                txVote.tip = vote.tip;
                txVote.voter = mnId.get();
                txVote.nRound = vote.nRound;
                txVote.choice = choice;

                this->voter->insertTxVote(txVote);
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
    if (isEnabled(GetAdjustedTime(), tip)) {
        const auto mnId{findMyMasternodeId()};
        LOCK(cs_main);

        if (!initialVotesDownload && mnId != boost::none && !this->voter->checkAmIVoter()) {
            LogPrintf("dpos: %s: I became a team member, enabling voter for me (I'm %s)\n", __func__, mnId.get().GetHex());
            this->voter->setVoting(true, mnId.get());
        } else if (mnId == boost::none && this->voter->checkAmIVoter()) {
            LogPrintf("dpos: %s: Disabling voter, I'm not a team member for now \n", __func__);
            this->voter->setVoting(false, CMasternode::ID{});
        }

        CValidationState state;
        this->voter->updateTip(tip);
        handleVoterOutput(this->voter->requestMissingTxs() + this->voter->doTxsVoting() + this->voter->doRoundVoting(), state);

        // periodically rm waste data from old blocks
        cleanUpDb();
    }
}

void CDposController::proceedViceBlock(const CBlock& viceBlock, CValidationState& state)
{
    bool success = false;
    LOCK(cs_main);
    vReqs.erase(CInv{MSG_VICE_BLOCK, viceBlock.GetHash()});
    {
        if (!findViceBlock(viceBlock.GetHash())) {
            const CDposVoterOutput out{voter->applyViceBlock(viceBlock)};

            if (handleVoterOutput(out, state)) {
                success = true;
            }
        }
    }
    if (success) {
        storeEntity(viceBlock, &CDposDB::WriteViceBlock, viceBlock.GetHash());
        relayEntity(viceBlock, MSG_VICE_BLOCK);
    }
}

void CDposController::proceedTransaction(const CTransaction& tx, CValidationState& state)
{
    LOCK(cs_main);
    vReqs.erase(CInv{MSG_TX, tx.GetHash()});
    handleVoterOutput(voter->applyTx(tx), state);
}

void CDposController::proceedRoundVote(const CRoundVote_p2p& vote, CValidationState& state)
{
    bool success = false;
    const uint256 voteHash{vote.GetHash()};
    LOCK(cs_main);
    {
        if (!findRoundVote(voteHash)) {
            this->receivedRoundVotes.emplace(voteHash, vote); // emplace before to be able to find signature to submit block
            if (acceptRoundVote(vote, state)) {
                success = true;
            } else {
                this->receivedRoundVotes.erase(voteHash);
            }
        }
    }
    if (success) {
        storeEntity(vote, &CDposDB::WriteRoundVote, voteHash);
        relayEntity(vote, MSG_ROUND_VOTE);
    }
}

void CDposController::proceedTxVote(const CTxVote_p2p& vote, CValidationState& state)
{
    bool success = false;
    const uint256 voteHash{vote.GetHash()};
    {
        LOCK(cs_main);
        if (!findTxVote(voteHash)) {
            if (acceptTxVote(vote, state)) {
                success = true;
            }
        }
    }
    if (success) {
        this->receivedTxVotes.emplace(voteHash, vote);
        storeEntity(vote, &CDposDB::WriteTxVote, voteHash);
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

std::vector<CTransaction> CDposController::listCommittedTxs(uint32_t maxdeep) const
{
    LOCK(cs_main);
    return this->voter->listCommittedTxs(this->voter->getTip(), 0, maxdeep).txs;
}

bool CDposController::isCommittedTx(const TxId& txid, uint32_t maxdeep) const
{
    LOCK(cs_main);
    return this->voter->isCommittedTx(txid, this->voter->getTip(), 0, maxdeep);
}

bool CDposController::isNotCommittableTx(const TxId& txid) const {
    LOCK(cs_main);
    return this->voter->isNotCommittableTx(txid);
}

bool CDposController::excludeTxFromBlock_miner(const CTransaction& tx) const {
    LOCK(cs_main);
    const std::vector<COutPoint> inputs = CDposVoter::getInputsOf(tx);
    for (const auto& in : inputs) {
        if (this->voter->pledgedInputs.count(in) > 0)
            return true;
    }
    return false;
}

Round CDposController::getCurrentVotingRound(int64_t time, const CBlockIndex* pindexTip) const
{
    if (isEnabled(time, pindexTip)) {
        AssertLockHeld(cs_main);
        return voter->getLowestNotOccupiedRound();
    }
    return 0;
}

Round CDposController::getCurrentVotingRound(int64_t time, int tipHeight) const
{
    if (isEnabled(time, tipHeight)) {
        AssertLockHeld(cs_main);
        return voter->getLowestNotOccupiedRound();
    }
    return 0;
}

Round CDposController::getCurrentVotingRound(int64_t time, const BlockHash& tipHash) const
{
    if (isEnabled(time, tipHash)) {
        AssertLockHeld(cs_main);
        return voter->getLowestNotOccupiedRound();
    }
    return 0;
}


CTxVotingDistribution CDposController::calcTxVotingStats(const TxId& txid) const
{
    LOCK(cs_main);
    return this->voter->calcTxVotingStats(txid, this->voter->getTip(), 1);
}

bool CDposController::isTxApprovedByMe(const TxId& txid) const
{
    LOCK(cs_main);
    return this->voter->isTxApprovedByMe(txid, this->voter->getTip());
}

bool CDposController::handleVoterOutput(const CDposVoterOutput& out, CValidationState& state)
{
    AssertLockHeld(cs_main);
    if (!out.vErrors.empty()) {
        if (chainActive.Height() < Params().GetConsensus().nMasternodesV2ForkHeight)
            return false;
        for (const auto& error : out.vErrors) {
            LogPrintf("dpos: %s: %s\n", __func__, error);
        }
        return state.DoS(IsInitialBlockDownload() ? 0 : 1, false, REJECT_INVALID, "dpos-msg-invalid");
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
                    const uint256 voteHash{vote.GetHash()};
                    this->receivedRoundVotes.emplace(voteHash, vote);
                    storeEntity(vote, &CDposDB::WriteRoundVote, voteHash);
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
                    const uint256 voteHash{vote.GetHash()};
                    this->receivedTxVotes.emplace(voteHash, vote);
                    storeEntity(vote, &CDposDB::WriteTxVote, voteHash);
                    relayEntity(vote, MSG_TX_VOTE);
                }
            }
        }

        for (const TxId& txReq : out.vTxReqs) {
            if (this->vReqs.size() >= MAX_INV_SZ)
                break;
            this->vReqs.emplace(MSG_TX, txReq);
        }
        for (const BlockHash& viceBlockReq : out.vViceBlockReqs) {
            if (this->vReqs.size() >= MAX_INV_SZ)
                break;
            this->vReqs.emplace(MSG_VICE_BLOCK, viceBlockReq);
        }

        if (out.blockToSubmit != boost::none) {
            CValidationState state_{};
            CBlockToSubmit blockToSubmit{out.blockToSubmit.get()};
            CBlock* pblock{&blockToSubmit.block};
            BlockHash blockHash = pblock->GetHash();

            for (const auto& votePair : this->receivedRoundVotes) {
                if (votePair.second.nRound == pblock->nRound &&
                    votePair.second.choice.decision == CVoteChoice::Decision::YES &&
                    votePair.second.choice.subject == blockHash &&
                    authenticateMsg(votePair.second, state_) != boost::none)
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
            } else if (!ProcessNewBlock(state_, nullptr, const_cast<CBlock*>(pblock), true, nullptr)) {
                LogPrintf("dpos: %s: Can't ProcessNewBlock\n", __func__);
            }
        }
    }

    return true;
}

bool CDposController::acceptRoundVote(const CRoundVote_p2p& vote, CValidationState& state)
{
    AssertLockHeld(cs_main);
    bool rv{true};
    const auto mnId{authenticateMsg(vote, state)};

    if (mnId == boost::none) {
        rv = false;
    } else {
        CRoundVote roundVote{};
        roundVote.tip = vote.tip;
        roundVote.voter = mnId.get();
        roundVote.nRound = vote.nRound;
        roundVote.choice = vote.choice;

        rv = handleVoterOutput(voter->applyRoundVote(roundVote), state);
    }

    return rv;
}

bool CDposController::acceptTxVote(const CTxVote_p2p& vote, CValidationState& state)
{
    if (vote.choices.size() != 1) {
        return false; // currently, accept only votes with 1 tx to avoid issues with partially accepted votes
    }

    AssertLockHeld(cs_main);
    bool rv{true};
    const auto mnId{authenticateMsg(vote, state)};

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

            if (!handleVoterOutput(voter->applyTxVote(txVote), state)) {
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

    CValidationState state;
    return getIdOfTeamMember(getTipHash(), mnIds->operatorAuthAddress, state);
}

boost::optional<CMasternode::ID> CDposController::getIdOfTeamMember(const BlockHash& blockHash, const CKeyID& operatorAuth, CValidationState& state)
{
    LOCK(cs_main);
    try {
        const int height{Validator::computeBlockHeight(blockHash, MAX_BLOCKS_TO_KEEP)};
        if (height == -1) {
            // block is unknown - maybe because we didn't sync yet
            state.DoS(IsInitialBlockDownload() ? 0 : 1, false, REJECT_INVALID, "dpos-msg-unknown-block");
            return boost::none;
        }

        const CTeam team = pmasternodesview->ReadDposTeam(height);
        for (auto&& member : team)
        {
            if (member.second.operatorAuth == operatorAuth)
                return {member.first};
        }
        if (team.empty()) {
            LogPrintf("dpos: Couldn't read dPoS team as it was already cleared \n");
        } else {
            // dPoS team was read, but operator wasn't found
            state.DoS(10, false, REJECT_INVALID, "dpos-msg-auth");
        }
    } catch (...) {
        return boost::none;
    }

    return boost::none;
}

boost::optional<CMasternode::ID> CDposController::authenticateMsg(const CTxVote_p2p& vote, CValidationState& state)
{
    CPubKey pubKey{};
    if (!pubKey.RecoverCompact(vote.GetSignatureHash(), vote.signature))
    {
        state.DoS(100, false, REJECT_INVALID, "dpos-txvote-sig-malformed");
        return boost::none;
    }
    return getIdOfTeamMember(vote.tip, pubKey.GetID(), state);
}

boost::optional<CMasternode::ID> CDposController::authenticateMsg(const CRoundVote_p2p& vote, CValidationState& state)
{
    CPubKey pubKey{};
    if (!pubKey.RecoverCompact(vote.GetSignatureHash(), vote.signature))
    {
        state.DoS(100, false, REJECT_INVALID, "dpos-rvote-sig-malformed");
        return boost::none;
    }
    return getIdOfTeamMember(vote.tip, pubKey.GetID(), state);
}

void CDposController::cleanUpDb()
{
    AssertLockHeld(cs_main);
    const auto tipHeight{chainActive.Height()};

    for (auto itV{this->voter->v.begin()}; itV != this->voter->v.end();) {
        const BlockHash& vot = itV->first;
        const int votingTipHeight{Validator::computeBlockHeight(vot, MAX_BLOCKS_TO_KEEP * 2)};

        // if unknown, or old
        if (votingTipHeight < 0 || ((tipHeight - votingTipHeight) > MAX_BLOCKS_TO_KEEP)) {
            for (auto it{this->receivedRoundVotes.begin()}; it != this->receivedRoundVotes.end();) {
                if (it->second.tip == vot) {
                    pdposdb->EraseRoundVote(it->first);
                    it = this->receivedRoundVotes.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto it{this->receivedTxVotes.begin()}; it != this->receivedTxVotes.end();) {
                if (it->second.tip == vot) {
                    pdposdb->EraseTxVote(it->first);
                    it = this->receivedTxVotes.erase(it);
                } else {
                    ++it;
                }
            }
            for (const auto& bpair: itV->second.viceBlocks) {
                pdposdb->EraseViceBlock(bpair.first);
            }
            itV = this->voter->v.erase(itV);
        } else {
            ++itV;
        }
    }
}

std::vector<TxId> CDposController::getTxsFilter() const
{
    std::vector<TxId> rv{};
    return rv;
}

} //namespace dpos
