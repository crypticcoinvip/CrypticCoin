// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "util.h"
#include "masternodes.h"
#include "../main.h"
#include "../key.h"
#include "../key_io.h"

namespace
{
    enum class LookupMode{ byOperator, byOwner };

    bool lookupMasternode(mns::CMasternodeIDs& mnId, LookupMode mode)
    {
        bool rv{false};

        if (mode == LookupMode::byOperator) {
            const auto it{pmasternodesview->nodesByOperator.find(mnId.operatorAuthAddress)};
            if (it != pmasternodesview->nodesByOperator.end()) {
                assert(pmasternodesview->allNodes.find(it->second) != pmasternodesview->allNodes.end());
                const CMasternode& mn{pmasternodesview->allNodes[it->second]};
                assert(mnId.operatorAuthAddress == mn.operatorAuthAddress);
                mnId.id = it->second;
                mnId.ownerAuthAddress = mn.ownerAuthAddress;
                rv = true;
            }
        } else if (mode == LookupMode::byOwner) {
            const auto it{pmasternodesview->nodesByOwner.find(mnId.ownerAuthAddress)};
            if (it != pmasternodesview->nodesByOwner.end()) {
                assert(pmasternodesview->allNodes.find(it->second) != pmasternodesview->allNodes.end());
                const CMasternode& mn{pmasternodesview->allNodes[it->second]};
                assert(mnId.ownerAuthAddress == mn.ownerAuthAddress);
                mnId.id = it->second;
                mnId.operatorAuthAddress = mn.operatorAuthAddress;
                rv = true;
            }
        }

        return rv;
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

void mns::mockMasternodesDB(const std::vector<std::string>& addresses, int activationMask)
{
    for (auto i = 0; i < addresses.size(); i++) {
        CMutableTransaction txx{};
        CMasternode node{};

        node.name = "Mnode_" + std::to_string(i + 1);
        node.ownerAuthAddress = boost::get<CKeyID>(DecodeDestination(addresses[i]));
        node.operatorAuthAddress = boost::get<CKeyID>(DecodeDestination(addresses[i + 1 < addresses.size() ? i + 1 : addresses.size() - i - 1]));
        node.ownerRewardAddress = GetScriptForDestination(DecodeDestination(addresses[i + 2 < addresses.size() ? i + 2 : addresses.size() - i]));

        node.height = 1;
        node.minActivationHeight = node.height + 100;
        node.activationHeight = -1;
        node.deadSinceHeight = -1;

        node.activationTx = uint256();
        node.collateralSpentTx = uint256();
        node.dismissFinalizedTx = uint256();

        node.counterVotesFrom = 0;
        node.counterVotesAgainst = 0;

        if ((activationMask & (1 << i)) == (1 << i)) {
            node.activationTx = txx.GetHash();
        }

        // We cannot check validness of 'ownerAuthAddress' or 'operatorAuthAddress'
        /// @todo @mn Check for serialization of metainfo
        if (!node.ownerRewardAddress.empty() &&
            !node.ownerAuthAddress.IsNull() &&
            !node.operatorAuthAddress.IsNull() &&
            (node.name.size() >= 3 && node.name.size() <= 255))
        {
            CScript scriptPubKey = GetScriptForDestination(node.ownerAuthAddress);
            CTxOut txout{22, scriptPubKey};
            txx.vout.push_back(txout);
            pmasternodesview->OnMasternodeAnnounce(txx.GetHash(), node);
        }
    }
    pmasternodesview->WriteBatch();
}
