// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_DPOS_H
#define BITCOIN_DPOS_H

#include "../chain.h"

namespace dpos
{
    bool checkActiveMode();
    void postBlockProgenitor(const CBlock& pblock);
    void relayBlockProgenitor(const CBlock& pblock);
    void recieveBlockProgenitor(const CBlock& pblock);
    const CBlock* getReceivedBlockProgenitor(const uint256& hash);
} // namespace dpos

#endif // BITCOIN_DPOS_H
