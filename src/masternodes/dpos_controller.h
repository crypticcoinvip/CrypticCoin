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
#include <net.h>

class CKeyID;
class CBlockIndex;
class CValidationInterface;
class CValidationState;

// Little helpers to PushMessage to nodes in a thread-safe manner
class CNodesShared : public std::vector<CNode*> {
public:
    CNodesShared() = default;
    CNodesShared(CNodesShared&&) = default;
    CNodesShared(const CNodesShared&) = delete;

    static CNodesShared getSharedList() {
        LOCK(cs_vNodes);
        CNodesShared vNodesCopy;
        for (CNode* pnode : vNodes) {
            vNodesCopy.emplace_back(pnode->AddRef());
        }
        return std::move(vNodesCopy);
    }

    ~CNodesShared() {
        LOCK(cs_vNodes);
        for (CNode* pnode : *this) {
            pnode->Release();
        }
    }
};

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

    bool isEnabled(int64_t time, const CBlockIndex* pindexTip) const;
    bool isEnabled(int64_t time, int tipHeight) const;
    bool isEnabled(int64_t time, const BlockHash& tipHash) const;

    CValidationInterface* getValidator();
    void loadDB();
    void onChainTipUpdated(const BlockHash& tipHash);

    Round getCurrentVotingRound(int64_t time, const CBlockIndex* pindexTip) const;
    Round getCurrentVotingRound(int64_t time, int tipHeight) const;
    Round getCurrentVotingRound(int64_t time, const BlockHash& tipHash) const;

    void proceedViceBlock(const CBlock& viceBlock, CValidationState& state);
    void proceedTransaction(const CTransaction& tx, CValidationState& state);
    void proceedRoundVote(const CRoundVote_p2p& vote, CValidationState& state);
    void proceedTxVote(const CTxVote_p2p& vote, CValidationState& state);

    bool findViceBlock(const BlockHash& hash, CBlock* block = nullptr) const;
    bool findRoundVote(const BlockHash& hash, CRoundVote_p2p* vote = nullptr) const;
    bool findTxVote(const BlockHash& hash, CTxVote_p2p* vote = nullptr) const;
    bool findTx(const TxId& txid, CTransaction* tx = nullptr) const;

    std::vector<CBlock> listViceBlocks() const;
    std::vector<CRoundVote_p2p> listRoundVotes() const;
    std::vector<CTxVote_p2p> listTxVotes() const;

    std::vector<CTransaction> listCommittedTxs(uint32_t maxdeep = CDposVoter::GUARANTEES_MEMORY) const;
    bool isCommittedTx(const TxId& txid, uint32_t maxdeep = CDposVoter::GUARANTEES_MEMORY) const;
    bool checkTxNotCommittable(const TxId& txid) const;
    bool excludeTxFromBlock_miner(const CTransaction& tx) const;
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

    bool handleVoterOutput(const CDposVoterOutput& out, CValidationState& state);
    bool acceptRoundVote(const CRoundVote_p2p& vote, CValidationState& state);
    bool acceptTxVote(const CTxVote_p2p& vote, CValidationState& state);

    void cleanUpDb();

    std::vector<TxId> getTxsFilter() const;

private:
    bool initialVotesDownload = true;
    std::shared_ptr<CDposVoter> voter;
    std::shared_ptr<Validator> validator;
    std::set<CInv> vReqs;
    std::map<uint256, CTxVote_p2p> receivedTxVotes;
    std::map<uint256, CRoundVote_p2p> receivedRoundVotes;
};


static CDposController * getController()
{
    return &CDposController::getInstance();
}

} // namespace dpos

#endif // MASTERNODES_DPOS_CONTROLLER_H

