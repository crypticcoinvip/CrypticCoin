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

using CVoteSignature = std::vector<unsigned char>;

class CVoteChoice
{
public:
    enum Decision
    {
        YES = 1, PASS = 2, NO = 3
    };

    uint256 subject;
    Decision decision;

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        const uint8_t decision8 = static_cast<uint8_t>(decision);
        READWRITE(subject);
        READWRITE(decision8);
        decision = static_cast<Decision>(decision8);
    }
};

class CTxVote_p2p
{
public:
    uint256 tip;
    uint16_t round;
    std::vector<CVoteChoice> choices;
    CVoteSignature signature;

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
    CVoteSignature signature;

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
