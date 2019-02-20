// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "dpos_p2p_messages.h"
#include "../hash.h"
#include "../version.h"

namespace dpos
{


namespace
{
std::array<unsigned char, 16> TX_VOTE_SALT{0x4D, 0x48, 0x7A, 0x52, 0x5D, 0x4D, 0x37, 0x78, 0x42, 0x36, 0x5B, 0x64,
                                   0x44, 0x79, 0x59, 0x4F};

std::array<unsigned char, 16> ROUND_VOTE_SALT{0x6A, 0x2A, 0x5E, 0x2D, 0x1D, 0x13, 0x0A, 0x12, 0x50, 0x72, 0x0A, 0x42,
                                   0x8F, 0xAC, 0x71, 0x34};
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief CTxVote_p2p::CTxVote_p2p

bool CTxVote_p2p::IsNull() const
{
    return round == 0;
}

void CTxVote_p2p::SetNull()
{
    tip.SetNull();
    round = 0;
    choices.clear();
    signature.clear();
}

uint256 CTxVote_p2p::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CTxVote_p2p::GetSignatureHash() const
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    ss << tip
       << round
       << choices
       << TX_VOTE_SALT;
    return Hash(ss.begin(), ss.end());
}

bool CTxVote_p2p::containsTx(const CTransaction& transaction) const
{
    return std::find_if(choices.begin(), choices.end(), [&](const CVoteChoice& vote)
    {
        return vote.hash == transaction.GetHash();
    }) != choices.end();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief CRoundVote::CRoundVote

bool CRoundVote::IsNull() const
{
    return round == 0;
}

void CRoundVote::SetNull()
{
    tip.SetNull();
    round = 0;
    choice.hash.SetNull();
    signature.clear();
}

uint256 CRoundVote::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CRoundVote::GetSignatureHash() const
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    ss << tip
       << round
       << choice
       << ROUND_VOTE_SALT;
    return Hash(ss.begin(), ss.end());
}

}