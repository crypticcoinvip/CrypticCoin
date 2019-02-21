#ifndef MASTERNODES_DPOS_TYPES_H
#define MASTERNODES_DPOS_TYPES_H

#include "../uint256.h"
#include "../arith_uint256.h"

namespace dpos
{

using Round = uint16_t;

}

using TxId = uint256;
using TxIdSorted = arith_uint256;
using BlockHash = uint256;

#endif //MASTERNODES_DPOS_TYPES_H

