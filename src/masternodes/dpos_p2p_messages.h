// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERNODES_DPOS_P2P_MESSAGES_H
#define MASTERNODES_DPOS_P2P_MESSAGES_H

#include "../uint256.h"
#include "../serialize.h"
#include "../primitives/transaction.h"
#include "dpos_types.h"

namespace dpos
{

class CVoteChoice
{
public:
    using Signature = std::vector<unsigned char>;

    enum Decision
    {
        YES = 1, PASS = 2, NO = 3
    };

    uint256 subject;
    int8_t decision;

    bool isStandardDecision() const
    {
        return decision == Decision::YES || decision == Decision::PASS || decision == Decision::NO;
    }

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(subject);
        READWRITE(decision);
    }
};

class CTxVote_p2p
{
public:
    static const int32_t CURRENT_VERSION = 1;
    int32_t nVersion = CURRENT_VERSION;
    uint256 tip;
    Round nRound;
    std::vector<CVoteChoice> choices;
    CVoteChoice::Signature signature;

    CTxVote_p2p()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(tip);
        READWRITE(nRound);
        READWRITE(choices);
        READWRITE(signature);
    }

    bool IsNull() const;
    void SetNull();

    uint256 GetHash() const;
    uint256 GetSignatureHash() const;

    bool containsTx(const CTransaction& transaction) const;
};

class CRoundVote_p2p
{
public:
    static const int32_t CURRENT_VERSION = 1;
    int32_t nVersion = CURRENT_VERSION;
    uint256 tip;
    Round nRound;
    CVoteChoice choice;
    CVoteChoice::Signature signature;

    CRoundVote_p2p()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(tip);
        READWRITE(nRound);
        READWRITE(choice);
        READWRITE(signature);
    }

    bool IsNull() const;
    void SetNull();

    uint256 GetHash() const;
    uint256 GetSignatureHash() const;
};

}

#endif //MASTERNODES_DPOS_P2P_MESSAGES_H
