// Copyright (c) 2016 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/wallet.h"
#include "crypticcoin/JoinSplit.hpp"
#include "crypticcoin/Note.hpp"
#include "crypticcoin/NoteEncryption.hpp"

CWalletTx GetValidReceive(ZCJoinSplit& params,
                          const libcrypticcoin::SpendingKey& sk, CAmount value,
                          bool randomInputs);
libcrypticcoin::Note GetNote(ZCJoinSplit& params,
                       const libcrypticcoin::SpendingKey& sk,
                       const CTransaction& tx, size_t js, size_t n);
CWalletTx GetValidSpend(ZCJoinSplit& params,
                        const libcrypticcoin::SpendingKey& sk,
                        const libcrypticcoin::Note& note, CAmount value);
