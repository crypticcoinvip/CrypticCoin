// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "dpos_validator.h"
#include "../main.h"
#include "../snark/libsnark/common/utils.hpp"

namespace dpos
{

/// @return false if tx cannot or shouldn't be added into a block
bool CDposController::Validator::validateTx(const CTransaction& tx)
{
    AssertLockHeld(cs_main);
    ScopedNoLogging noLogging;

    if (!tx.fInstant)
        return false;

    {
        // tx is already included into a block
        BlockHash txBlockHash{};
        CTransaction notUsed;
        if (GetTransaction(tx.GetHash(), notUsed, txBlockHash, false) && !txBlockHash.IsNull())
            return false;
    }

    CValidationState state;
    int nextBlockHeight = chainActive.Height() + 1;

    auto verifier = libzcash::ProofVerifier::Strict();
    if (!CheckTransaction(tx, state, verifier))
        return error("validateTx: CheckTransaction failed");

    // Check transaction contextually against the set of consensus rules which apply in the next block to be mined.
    if (!ContextualCheckTransaction(tx, state, nextBlockHeight, 10)) {
        return error("validateTx: ContextualCheckTransaction failed");
    }

    // DoS mitigation: reject transactions expiring soon
    if (IsExpiringSoonTx(tx, nextBlockHeight)) {
        return state.DoS(0, error("validateTx(): transaction is expiring soon"), REJECT_INVALID, "tx-expiring-soon");
    }

    return true;
}

/// @return false if tx cannot be added into a block
bool CDposController::Validator::validateTxs(const std::map<TxIdSorted, CTransaction>& txMap)
{
    AssertLockHeld(cs_main);
    ScopedNoLogging noLogging;

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

    // insert txs (which we validate) into the block
    for (const auto& tx_p : txMap)
        block.vtx.push_back(tx_p.second);

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

bool CDposController::Validator::validateBlock(const CBlock& block, const std::map<TxIdSorted, CTransaction>& instantTxsExpected, bool fJustCheckPoW)
{
    AssertLockHeld(cs_main);
    ScopedNoLogging noLogging;
    CValidationState state;

    if (fJustCheckPoW) {
        assert(instantTxsExpected.empty());
        if (!CheckBlockHeader(block, state, true))
            return false;
        return ContextualCheckBlockHeader(block, state, chainActive.Tip());
    }

    // check instant txs are equal to the template
    const size_t instSectionStart = 1;
    size_t instSectionEnd = 0;
    for (unsigned int i = instSectionStart; i < block.vtx.size(); i++) {
        const CTransaction &tx = block.vtx[i];
        if (!tx.fInstant) {
            // txs order will be checked inside TestBlockValidity
            instSectionEnd = i;
            break;
        }
    }
    if (instSectionEnd == 0) { // didn't meet !tx.fInstant => all the txs are instant
        instSectionEnd = block.vtx.size();
    }
    if (!instantTxsExpected.empty()) {
        // check size
        const size_t instTxsNum = instSectionEnd - instSectionStart;
        if (instTxsNum != instantTxsExpected.size())
            return false;
        // compare txs
        auto matchInstTx_it = instantTxsExpected.begin();
        for (size_t i = instSectionStart; i < instSectionEnd; i++) {
            if (block.vtx[i] != matchInstTx_it->second)
                return false;
            matchInstTx_it++;
        }
    }

    // check block validity
    DposValidationRules dvr{};
    dvr.fCheckDposSigs = false;
    return TestBlockValidity(state, block, chainActive.Tip(), true, true, dvr);
}

bool CDposController::Validator::allowArchiving(const BlockHash& blockHash)
{
    assert(chainActive.Tip() != nullptr);
    return chainActive.Height() - computeBlockHeight(blockHash, MIN_BLOCKS_TO_KEEP) < MIN_BLOCKS_TO_KEEP;
}

int CDposController::Validator::computeBlockHeight(const BlockHash& blockHash, int maxDeep)
{
    int rv{-1};
    assert(chainActive.Tip() != nullptr);

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
        getController()->proceedTransaction(tx);
    }
}

} // namespace dpos
