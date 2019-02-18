// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODES_UTIL_H
#define BITCOIN_MASTERNODES_UTIL_H

#include "../uint256.h"
#include "../pubkey.h"
#include "../key.h"
#include <boost/optional.hpp>

namespace mns
{

struct CMasternodeIDs {
    uint256 id;
    CKeyID operatorAuthAddress;
    CKeyID ownerAuthAddress;
};

boost::optional<CMasternodeIDs> amIOperator();
boost::optional<CMasternodeIDs> amIActiveOperator();
boost::optional<CMasternodeIDs> amIOwner();
boost::optional<CMasternodeIDs> amIActiveOwner();

CKey getOperatorKey();

void mockMasternodesDB(const std::vector<std::string>& addresses, int activationMask = 0);

} // namespace mns

#endif // BITCOIN_MASTERNODES_UTIL_H
