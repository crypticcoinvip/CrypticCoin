// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef MASTERNODES_DPOS_CONTROLLER_H
#define MASTERNODES_DPOS_CONTROLLER_H

#include "dpos_p2p_messages.h"
#include "dpos_voter.h"
#include "../primitives/block.h"
#include <map>
#include <memory>
#include <protocol.h>

class CKeyID;
class CValidationInterface;

namespace dpos
{
class CDposVoter;
struct CDposVoterOutput;

class CDposController
{
    class Validator;

public:
    static CDposController& getInstance();
    static void runEventLoop();

    bool isEnabled(int tipHeight = -1) const;
    bool isEnabled(const BlockHash& tipHash) const;

    CValidationInterface* getValidator();
    void loadDB();
    void onChainTipUpdated(const BlockHash& tipHash);

    Round getCurrentVotingRound() const;

    void proceedViceBlock(const CBlock& viceBlock);
    void proceedTransaction(const CTransaction& tx);
    void proceedRoundVote(const CRoundVote_p2p& vote);
    void proceedTxVote(const CTxVote_p2p& vote);

    bool findViceBlock(const BlockHash& hash, CBlock* block = nullptr) const;
    bool findRoundVote(const BlockHash& hash, CRoundVote_p2p* vote = nullptr) const;
    bool findTxVote(const BlockHash& hash, CTxVote_p2p* vote = nullptr) const;
    bool findTx(const TxId& txid, CTransaction* tx = nullptr) const;

    std::vector<CBlock> listViceBlocks() const;
    std::vector<CRoundVote_p2p> listRoundVotes() const;
    std::vector<CTxVote_p2p> listTxVotes() const;

    std::vector<CTransaction> listCommittedTxs() const;
    bool isCommittedTx(const TxId& txid) const;
    bool isTxApprovedByMe(const TxId& txid) const;
    CTxVotingDistribution calcTxVotingStats(const TxId& txid) const;

private:
    static boost::optional<CMasternode::ID> findMyMasternodeId();
    static boost::optional<CMasternode::ID> getIdOfTeamMember(const BlockHash& blockHash, const CKeyID& operatorAuth);

    static boost::optional<CMasternode::ID> authenticateMsg(const CTxVote_p2p& vote);
    static boost::optional<CMasternode::ID> authenticateMsg(const CRoundVote_p2p& vote);

    CDposController() = default;
    ~CDposController() = default;
    CDposController(const CDposController&) = delete;
    CDposController(CDposController&&) = delete;
    CDposController& operator =(const CDposController&) = delete;

    bool handleVoterOutput(const CDposVoterOutput& out);
    bool acceptRoundVote(const CRoundVote_p2p& vote);
    bool acceptTxVote(const CTxVote_p2p& vote);

    void removeOldVotes();

    std::vector<TxId> getTxsFilter() const;

private:
    bool initialVotesDownload = true;
    std::shared_ptr<CDposVoter> voter;
    std::shared_ptr<Validator> validator;
    std::set<CInv> vTxReqs;
    std::map<uint256, CTxVote_p2p> receivedTxVotes;
    std::map<uint256, CRoundVote_p2p> receivedRoundVotes;
};


static CDposController * getController()
{
    return &CDposController::getInstance();
}

} // namespace dpos

#endif // MASTERNODES_DPOS_CONTROLLER_H

