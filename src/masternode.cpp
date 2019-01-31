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

bool CMasternodeIDs::operator !=(const CMasternodeIDs& rhs) const
{
    return txId != rhs.txId ||
           ownerAuth != rhs.ownerAuth ||
           operatorAuth != rhs.operatorAuth;
}

bool CMasternodeIDs::operator ==(const CMasternodeIDs& rhs) const
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
        LogPrintf("-->-->%s == %s\n", operatorAuth.GetHex(), mn.operatorAuth.GetHex());

        if ((!txId.IsNull() && txId == mn.txId) ||
            (!ownerAuth.IsNull() && ownerAuth == mn.ownerAuth) ||
            (!operatorAuth.IsNull() && operatorAuth == mn.operatorAuth))
        {
           return std::move(mn);
        }
    }

    return CMasternodeIDs{};
}

std::vector<CMasternode> mns::getMasternodeList(CMasternodeIDs idsFilter)
{
    CMasternode mn{};
    std::vector<CMasternode> rv{};

    const auto addFiltered{[&](int blockHeight) {
        if (idsFilter.isNull() ||
            idsFilter.txId == mn.txId ||
            idsFilter.ownerAuth == mn.ownerAuth ||
            idsFilter.operatorAuth == mn.operatorAuth )
        {
            mn.announcementBlockHeight = blockHeight;
            rv.emplace_back(mn);
        }
    }};

    mn.txId.SetHex("a1c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.ownerAuth.SetHex("b1c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.operatorAuth.SetHex("4f3ca2389b8bfc447c2ece3f62d9df7b3f820def");
    addFiltered(101);

    mn.txId.SetHex("a2c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.ownerAuth.SetHex("b2c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.operatorAuth.SetHex("a62435f55d5a800938d25f38be553f11dba210f0");
    addFiltered(102);

    mn.txId.SetHex("a3c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.ownerAuth.SetHex("b3c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.operatorAuth.SetHex("ba3cd763646c4e40cec69a09c89f72bbdb9a01fc");
    addFiltered(103);

    mn.txId.SetHex("a4c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.ownerAuth.SetHex("b4c70c4a88205065c1d33b17c156137fa8c736c1");
    mn.operatorAuth.SetHex("7a269ba7e8e7506bb273051c6eb46ae29bbdc5f6");
    addFiltered(104);

//    if (GetBoolArg("-masternode", false)) {
//        CAccount account{};
//        CWalletDB walletdb{pwalletMain->strWalletFile};

//        if (walletdb.ReadAccount("", account)) {
//            mn.txId.SetHex("ff170c4a88205065c1d33b17c156137fa8c736c1");
//            mn.ownerAuth.SetHex("ff270c4a88205065c1d33b17c156137fa8c736c1");
//            mn.operatorAuth = account.vchPubKey.GetID();
//            addFiltered(111);
//        }
//    }

    return rv;
}

int64_t CMasternode::getAnnounceBlockTime() const
{
    return 1548706221000ll + announcementBlockHeight * 1000;
}
