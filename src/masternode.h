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
    bool operator !=(const CMasternodeIDs& rhs) const;
    bool operator ==(const CMasternodeIDs& rhs) const;
};

struct CMasternode : public CMasternodeIDs
{
    int announcementBlockHeight;

    std::int64_t getAnnounceBlockTime() const;
};

namespace mns
{
    CMasternodeIDs amIOwner();
    CMasternodeIDs amIOperator();
    CMasternodeIDs findMasternode(const uint256& txId, const CKeyID& ownerAuth, const CKeyID& operatorAuth);
    std::vector<CMasternode> getMasternodeList(CMasternodeIDs idsFilter = CMasternodeIDs{});
}

#endif

