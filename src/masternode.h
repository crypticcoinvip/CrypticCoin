// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODE_H
#define BITCOIN_MASTERNODE_H

#include "pubkey.h"
#include <boost/optional.hpp>

struct CMasternodeIDs
{
    uint256 txId;
    CKeyID ownerAuth;
    CKeyID operatorAuth;

    virtual bool isNull() const;
    bool operator !=(const CMasternodeIDs& rhs);
    bool operator ==(const CMasternodeIDs& rhs);
};

struct CMasternode : public CMasternodeIDs
{
    int announcementBlockHeight;
};

namespace mns
{
//    CMasternodeIDs amIMasternode();
    CMasternodeIDs amIOwner();
    CMasternodeIDs amIOperator();
    CMasternodeIDs findMasternode(const uint256& txId, const CKeyID& ownerAuth, const CKeyID& operatorAuth);
    int getMasternodeCount();
    std::vector<CMasternode> getMasternodeList();
//    std::int64_t getOutdatedMasternodeTime(const CMasternodeIDs& id);
}

#endif

