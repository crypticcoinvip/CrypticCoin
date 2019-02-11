// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODES_DPOS_H
#define BITCOIN_MASTERNODES_DPOS_H

#include "../chain.h"

class CValidationInterface;

namespace dpos
{
    class ProgenitorVote
    {
        using Signature = std::vector<unsigned char>;

    public:
        uint256 dposBlockHash;
        uint16_t roundNumber;
        uint256 tipBlockHash;
        uint256 progenitorBlockHash;
        Signature authSignature;

        ProgenitorVote();

        ADD_SERIALIZE_METHODS;

        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action)
        {
            READWRITE(dposBlockHash);
            READWRITE(roundNumber);
            READWRITE(tipBlockHash);
            READWRITE(progenitorBlockHash);
            READWRITE(authSignature);
        }

        bool IsNull() const;
        void SetNull();

        uint256 GetHash() const;
    };

    bool checkIsActive();

    void postProgenitorBlock(const CBlock& pblock);
    void relayProgenitorBlock(const CBlock& pblock);
    bool recieveProgenitorBlock(const CBlock& pblock, bool isMe);
    const CBlock* getReceivedProgenitorBlock(const uint256& hash);
    std::vector<CBlock> listReceivedProgenitorBlocks();

    void postProgenitorVote(const ProgenitorVote& vote);
    void relayProgenitorVote(const ProgenitorVote& vote);
    bool recieveProgenitorVote(const ProgenitorVote& vote, bool isMe);
    const ProgenitorVote* getReceivedProgenitorVote(const uint256& hash);
    std::vector<ProgenitorVote> listReceivedProgenitorVotes();

    CValidationInterface * getValidationListener();
} // namespace dpos

#endif // BITCOIN_MASTERNODES_DPOS_H
