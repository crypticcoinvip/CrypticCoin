// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "dpos_validator.h"
#include "../main.h"
#include "../snark/libsnark/common/utils.hpp"

namespace dpos {

BlockHash CDposController::Validator::getPrevBlock(const BlockHash &blockHash)
{
    AssertLockHeld(cs_main);
    const int height = computeBlockHeight(blockHash, MAX_BLOCKS_TO_KEEP);
    if (height <= 0)
        return BlockHash{};
    return chainActive[height - 1]->GetBlockHash();
}

/// @return false if tx is unusable in any future block
bool CDposController::Validator::preValidateTx(const CTransaction& tx, uint32_t txExpiringSoonThreshold)
{
    AssertLockHeld(cs_main);
    ScopedNoLogging noLogging; // suppress logs

    if (!tx.fInstant)
        return false;

    if (!tx.vjoinsplit.empty()) // no sprout txs
        return false;

    {
        std::vector<unsigned char> metadata_dummy;
        if (GuessMasternodeTxType(tx, metadata_dummy) != MasternodesTxType::None)
            return error("validateTx: masternode-specific txs cannot be instant");
    }

    CValidationState state;
    const int nextBlockHeight = chainActive.Height() + 1;

    auto verifier = libzcash::ProofVerifier::Strict();

    if (!CheckTransaction(tx, state, verifier))
        return error("validateTx: CheckTransaction failed");

    // Check transaction contextually against the set of consensus rules which apply in the next block to be mined.
    if (!ContextualCheckTransaction(tx, state, nextBlockHeight, 10)) {
        return error("validateTx: ContextualCheckTransaction failed");
    }

    // DoS mitigation: reject transactions expiring soon
    if (IsExpiredTx(tx, nextBlockHeight + txExpiringSoonThreshold)) {
        return state.DoS(0, error("validateTx(): transaction is expiring soon"), REJECT_INVALID, "tx-expiring-soon");
    }

    return true;
}

/// @return false if txs cannot be added into next block
bool CDposController::Validator::validateTx(const CTransaction& tx)
{
    AssertLockHeld(cs_main);
    ScopedNoLogging noLogging; // suppress logs

    if (!preValidateTx(tx, 1))
        return false;

    // create dummy block
    CBlock block;
    // insert dummy coinbase
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(Params().GetConsensus(), chainActive.Height());
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = CScript() << (chainActive.Height() + 1) << OP_0;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = CScript() << OP_RETURN;
    txNew.vout[0].nValue = GetBlockSubsidy(chainActive.Height(), Params().GetConsensus());
    txNew.nExpiryHeight = 0;
    block.vtx.emplace_back(txNew);

    // insert tx (which we validate) into the block
    block.vtx.push_back(tx);

    // check block validity
    CValidationState state;
    DposValidationRules dvr{};
    dvr.fCheckDposSigs = false;
    dvr.fCheckInstSection = true;
    dvr.fCheckDposReward = false;
    dvr.fCheckSaplingRoot = false;
    dvr.nMaxInstsSigops = MAX_INST_SECTION_SIGOPS / 2; // vote No for txs if they get close to the limit
    dvr.nMaxInstsSize = MAX_INST_SECTION_SIZE / 2; // vote No for txs if they get close to the limit

    CCoinsViewCache viewNew(pcoinsTip);
    // Read-only copy of masternodesview. But anyway, it runs 'ConnectBlock' with 'justCheck' here
    CMasternodesView mnview(*pmasternodesview);
    CBlockIndex indexDummy(block);
    indexDummy.pprev = chainActive.Tip();
    indexDummy.nHeight = chainActive.Tip()->nHeight + 1;
    uint256 blockHash = block.GetHash();
    indexDummy.phashBlock = &blockHash;

    if (!ContextualCheckBlock(block, state, chainActive.Tip()))
        return false;
    if (!ConnectBlock(block, state, &indexDummy, viewNew, mnview, true, dvr))
        return false;

    return true;

    //return TestBlockValidity(state, block, chainActive.Tip(), false, false, dvr);
}

/// @return false if the block cannot be connected
bool CDposController::Validator::validateBlock(const CBlock& block, bool fJustCheckPoW)
{
    AssertLockHeld(cs_main);
    ScopedNoLogging noLogging; // suppress logs
    CValidationState state;

    if (fJustCheckPoW) {
        if (!CheckBlockHeader(block, state, true))
            return false;
        return ContextualCheckBlockHeader(block, state, chainActive.Tip());
    }

    // check block validity
    DposValidationRules dvr{};
    dvr.fCheckDposSigs = false;
    return TestBlockValidity(state, block, chainActive.Tip(), true, true, dvr);
}

bool CDposController::Validator::allowArchiving(const BlockHash& blockHash)
{
    if (chainActive.Tip() == nullptr)
        return true;
    return chainActive.Height() - computeBlockHeight(blockHash, MAX_BLOCKS_TO_KEEP) < MAX_BLOCKS_TO_KEEP;
}

int CDposController::Validator::computeBlockHeight(const BlockHash& blockHash, int maxDeep)
{
    int rv{-1};
    if (chainActive.Tip() == nullptr)
        return rv;

    if (blockHash == chainActive.Tip()->GetBlockHash()) {
        rv = chainActive.Height();
    } else {
        for (CBlockIndex* index{chainActive.Tip()}; index != nullptr; index = index->pprev) {
            if (blockHash == index->GetBlockHash()) {
                rv = index->nHeight;
                break;
            }
            if (maxDeep > 0) {
                maxDeep--;
            }
            if (maxDeep == 0) {
                break;
            }
        }
    }
    return rv;
}

void CDposController::Validator::UpdatedBlockTip(const CBlockIndex* pindex)
{
    assert(pindex != nullptr);
    getController()->onChainTipUpdated(pindex->GetBlockHash());
}

void CDposController::Validator::SyncTransaction(const CTransaction& tx, const CBlock* pblock)
{
    libsnark::UNUSED(pblock);
    LOCK(cs_main);
    if (mempool.exists(tx.GetHash()) && tx.fInstant) {
        CValidationState state;
        getController()->proceedTransaction(tx, state);
    }
}

} // namespace dpos
