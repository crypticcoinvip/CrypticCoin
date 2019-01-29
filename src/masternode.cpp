// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode.h"
#include "util.h"
#include "init.h"
#include "wallet/wallet.h"

const CKeyID& CMasternode::getAddress() const
{
    return address;
}

CMasternode* CMasternode::amIMasternode()
{
    static CMasternode* result{nullptr};

    if (GetBoolArg("-masternode", false)) {
        result = new CMasternode{};
        LOCK2(cs_main, pwalletMain->cs_wallet);
        assert(!pwalletMain->GetAccountAddresses("").empty());
        result->address = boost::get<CKeyID>(*pwalletMain->GetAccountAddresses("").begin());
    }

    return result;
}

std::vector<CMasternode*> CMasternode::getAvailableList()
{
    static std::vector<CMasternode> list{};
    std::vector<CMasternode*> rv{};

    if (list.empty()) {
        CMasternode mn{};
        mn.address.SetHex("a1c70c4a88205065c1d33b17c156137fa8c736c1");
        list.emplace_back(mn);
        mn.address.SetHex("b2c70c4a88205065c1d33b17c156137fa8c736c1");
        list.emplace_back(mn);
        mn.address.SetHex("c3c70c4a88205065c1d33b17c156137fa8c736c1");
        list.emplace_back(mn);
        mn.address.SetHex("d4c70c4a88205065c1d33b17c156137fa8c736c1");
        list.emplace_back(mn);
        mn.address.SetHex("e5c70c4a88205065c1d33b17c156137fa8c736c1");
        list.emplace_back(mn);
    }
    for (auto&& mn : list) {
        rv.push_back(&mn);
    }
    auto iam = amIMasternode();
    if (iam != nullptr) {
        rv.push_back(iam);
    }
    return rv;
}
