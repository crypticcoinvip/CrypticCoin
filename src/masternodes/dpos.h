// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODES_DPOS_H
#define BITCOIN_MASTERNODES_DPOS_H

#include "../chain.h"

class CDataStream;

namespace dpos
{
    class ProgenitorVote
    {
        using Signature = std::vector<unsigned char>;

    public:
        uint256 headerBlockHash;
        uint16_t roundNumber;
        Signature headerSignature;
        uint256 tipBlockHash;
        uint256 progenitorBlockHash;
        Signature bodySignature;

        ProgenitorVote();

        ADD_SERIALIZE_METHODS;

        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(headerBlockHash);
            READWRITE(roundNumber);
            READWRITE(headerSignature);
            READWRITE(tipBlockHash);
            READWRITE(progenitorBlockHash);
            READWRITE(bodySignature);
        }

        bool IsNull() const;
        void SetNull();

        uint256 GetHash() const;
    };

    bool checkIsActive();

    void postProgenitorBlock(const CBlock& pblock);
    void relayProgenitorBlock(const CBlock& pblock);
    void recieveProgenitorBlock(const CBlock& pblock);
    const CBlock* getReceivedProgenitorBlock(const uint256& hash);

    void postProgenitorVote(const ProgenitorVote& vote);
    void relayProgenitorVote(const ProgenitorVote& vote);
    void recieveProgenitorVote(const ProgenitorVote& vote);
    const ProgenitorVote* getReceivedProgenitorVote(const uint256& hash);
} // namespace dpos

#endif // BITCOIN_MASTERNODES_DPOS_H
