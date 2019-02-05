// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "util.h"
#include "masternodes.h"
#include "../main.h"
#include "../key.h"

namespace
{
    enum class LookupMode{ byOperator, byOwner };

    bool lookupMasternode(mns::CMasternodeIDs& mnId, LookupMode mode)
    {
        if (mode == LookupMode::byOperator) {
            const auto it{pmasternodesview->nodesByOperator.find(mnId.operatorAuthAddress)};
            if (it != pmasternodesview->nodesByOperator.end()) {
                assert(pmasternodesview->allNodes.find(it->second) != pmasternodesview->allNodes.end());
                const CMasternode& mn{pmasternodesview->allNodes[it->second]};
                assert(mnId.operatorAuthAddress == mn.operatorAuthAddress);
                mnId.id = it->second;
                mnId.ownerAuthAddress = mn.ownerAuthAddress;
                return true;
            }
        } else if (mode == LookupMode::byOwner) {
            const auto it{pmasternodesview->nodesByOwner.find(mnId.ownerAuthAddress)};
            if (it != pmasternodesview->nodesByOwner.end()) {
                assert(pmasternodesview->allNodes.find(it->second) != pmasternodesview->allNodes.end());
                const CMasternode& mn{pmasternodesview->allNodes[it->second]};
                assert(mnId.ownerAuthAddress == mn.ownerAuthAddress);
                mnId.id = it->second;
                mnId.operatorAuthAddress = mn.operatorAuthAddress;
                return true;
            }
        }
    }
}

boost::optional<mns::CMasternodeIDs> mns::amIOperator()
{
    CMasternodeIDs mnId{};
    boost::optional<CMasternodeIDs> rv{};

    mnId.operatorAuthAddress.SetHex(GetArg("-masternode-operator", ""));
    if (lookupMasternode(mnId, LookupMode::byOperator)) {
        rv = mnId;
    }

    return rv;
}

boost::optional<mns::CMasternodeIDs> mns::amIActiveOperator()
{
    return amIOperator();
}

boost::optional<mns::CMasternodeIDs> mns::amIOwner()
{
    CMasternodeIDs mnId{};
    boost::optional<CMasternodeIDs> rv{};

    mnId.ownerAuthAddress.SetHex(GetArg("-masternode-owner", ""));
    if (lookupMasternode(mnId, LookupMode::byOwner)) {
        rv = mnId;
    }

    return rv;
}

boost::optional<mns::CMasternodeIDs> mns::amIActiveOwner()
{
    return amIOwner();
}
