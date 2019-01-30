// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode.h"
#include "util.h"
#include "init.h"
#include "wallet/wallet.h"

bool CMasternodeIDs::isNull() const
{
    return txId.IsNull() &&
           ownerAuth.IsNull() &&
           operatorAuth.IsNull();
}

bool CMasternodeIDs::operator !=(const CMasternodeIDs& rhs)
{
    return txId != rhs.txId ||
           ownerAuth != rhs.ownerAuth ||
            operatorAuth != rhs.operatorAuth;
}

bool CMasternodeIDs::operator ==(const CMasternodeIDs& rhs)
{
    return !(*this != rhs);
}


CMasternodeIDs mns::amIOwner()
{
    CMasternodeIDs ids{};
    ids.ownerAuth.SetHex(GetArg("-masternode-owner", ""));
    return findMasternode(ids.txId, ids.ownerAuth, ids.operatorAuth);
}

CMasternodeIDs mns::amIOperator()
{
    CMasternodeIDs ids{};
    ids.operatorAuth.SetHex(GetArg("-masternode-operator", ""));
    return findMasternode(ids.txId, ids.ownerAuth, ids.operatorAuth);
}

CMasternodeIDs mns::findMasternode(const uint256& txId, const CKeyID& ownerAuth, const CKeyID& operatorAuth)
{
    for (auto&& mn : getMasternodeList()) {
        assert(!mn.isNull());

        if ((!txId.IsNull() && txId == mn.txId) ||
            (!ownerAuth.IsNull() && ownerAuth == mn.ownerAuth) ||
            (!operatorAuth.IsNull() && operatorAuth == mn.operatorAuth))
        {
           return std::move(mn);
        }
    }

    return CMasternodeIDs{};
}


int mns::getMasternodeCount()
{
    return getMasternodeList().size();
}

std::vector<CMasternode> mns::getMasternodeList()
{
    CMasternode mn{};
    std::vector<CMasternode> rv{};

    mn.txId.SetHex("a1c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.ownerAuth.SetHex("b1c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.operatorAuth.SetHex("c1c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.announcementBlockHeight = 101;
    rv.emplace_back(mn);

    mn.txId.SetHex("a2c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.ownerAuth.SetHex("b2c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.operatorAuth.SetHex("c2c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.announcementBlockHeight = 102;
    rv.emplace_back(mn);

    mn.txId.SetHex("a3c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.ownerAuth.SetHex("b2c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.operatorAuth.SetHex("c3c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.announcementBlockHeight = 103;
    rv.emplace_back(mn);

    return rv;
}


