// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODE_H
#define BITCOIN_MASTERNODE_H

#include "pubkey.h"

class CMasternode
{
public:
    const CKeyID& getAddress() const;

    static CMasternode* amIMasternode();

    static std::vector<CMasternode*> getAvailableList();

private:
    CMasternode() = default;

private:
    CKeyID address;
};



#endif

