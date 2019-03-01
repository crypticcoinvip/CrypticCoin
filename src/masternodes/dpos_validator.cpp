// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "dpos_validator.h"
#include "../main.h"
#include "../snark/libsnark/common/utils.hpp"

namespace dpos
{

bool CDposController::Validator::validateTx(const std::map<TxIdSorted, CTransaction>& txMap)
{
    AssertLockHeld(cs_main);
    // NOT SUPPOSED TO BE USED AN ACTUAL VOTER! Only by relaying node.

    return true;
}

bool CDposController::Validator::validateBlock(const CBlock& block, const std::map<TxIdSorted, CTransaction>& txMap, bool flag)
{
    AssertLockHeld(cs_main);
    // NOT SUPPOSED TO BE USED AN ACTUAL VOTER! Only by relaying node.

    ScopedNoLogging noLogging;
    CValidationState state;
    return CheckBlockHeader(block, state, true);
}

bool CDposController::Validator::allowArchiving(BlockHash blockHash)
{
    AssertLockHeld(cs_main);
    return true;
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
