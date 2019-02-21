// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef DPOS_P2P_MESSAGES_H
#define DPOS_P2P_MESSAGES_H

#include "../uint256.h"
#include "../serialize.h"
#include "../primitives/transaction.h"

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
    uint256 tip;
    uint16_t round;
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
        READWRITE(tip);
        READWRITE(round);
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
    uint256 tip;
    uint16_t round;
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
        READWRITE(round);
        READWRITE(choice);
        READWRITE(signature);
    }

    bool IsNull() const;
    void SetNull();

    uint256 GetHash() const;
    uint256 GetSignatureHash() const;
};

}

#endif //DPOS_P2P_MESSAGES_H
