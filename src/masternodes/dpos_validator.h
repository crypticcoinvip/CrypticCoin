// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERNODES_DPOS_VALIDATOR_H
#define MASTERNODES_DPOS_VALIDATOR_H

#include "dpos_controller.h"
#include "dpos_types.h"
#include "../validationinterface.h"
#include "../consensus/validation.h"

namespace dpos
{

static const unsigned int MAX_BLOCKS_TO_KEEP = 8;

class CDposController::Validator : public CValidationInterface
{
public:
    bool validateTx(const CTransaction& tx);
    bool preValidateTx(const CTransaction& tx, uint32_t txExpiringSoonThreshold);
    bool validateBlock(const CBlock& block, bool fJustCheckPoW);
    bool allowArchiving(const BlockHash& blockHash);
    BlockHash getPrevBlock(const BlockHash& blockHash);

    static int computeBlockHeight(const BlockHash& blockHash, int maxDeep = -1);

protected:
    void UpdatedBlockTip(const CBlockIndex *pindex) override;
    void SyncTransaction(const CTransaction& tx, const CBlock* pblock) override;
};

} // namespace dpos
#endif //MASTERNODES_DPOS_VALIDATOR_H
