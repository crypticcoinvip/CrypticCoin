// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODES_DPOS_TYPES_H
#define MASTERNODES_DPOS_TYPES_H

#include "../uint256.h"
#include "../arith_uint256.h"

namespace dpos
{

using Round = uint32_t;

}

using TxId = uint256;
using TxIdSorted = arith_uint256;
using BlockHash = uint256;

#endif //MASTERNODES_DPOS_TYPES_H

