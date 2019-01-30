// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternodes.h"

#include "primitives/block.h"
#include "txdb.h"

static const std::vector<unsigned char> MnTxMarker = {'M', 'n', 'T', 'x'};
static const std::map<char, MasternodesTxType> MasternodesTxTypeToCode =
{
    {'a', MasternodesTxType::AnnounceMasternode },
    {'A', MasternodesTxType::ActivateMasternode },
    {'O', MasternodesTxType::SetOperatorReward },
    {'V', MasternodesTxType::DismissVote },
    {'v', MasternodesTxType::DismissVoteRecall }
    // Without CollateralSpent
};


/*
 * Checks if given tx is probably one of 'MasternodeTx', returns tx type and serialized metadata in 'data'
*/
MasternodesTxType GuessMasternodeTxType(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    CScript const & memo = tx.vout[0].scriptPubKey;
    CScript::const_iterator pc = memo.begin();
    opcodetype opcode;
    if (!memo.GetOp(pc, opcode) || opcode != OP_RETURN)
    {
        return MasternodesTxType::None;;
    }
    if (!memo.GetOp(pc, opcode, metadata) ||
            (opcode != OP_PUSHDATA1 &&
             opcode != OP_PUSHDATA2 &&
             opcode != OP_PUSHDATA4) ||
            metadata.size() < MnTxMarker.size() + 1 ||     // i don't know how much exactly, but at least MnTxSignature + type prefix
            memcmp(&metadata[0], &MnTxMarker[0], MnTxMarker.size()) != 0)
    {
        return MasternodesTxType::None;
    }
    auto const & it = MasternodesTxTypeToCode.find(metadata[MnTxMarker.size()]);
    if (it == MasternodesTxTypeToCode.end())
    {
        return MasternodesTxType::None;
    }
    metadata.erase(metadata.begin(), metadata.begin() + MnTxMarker.size() + 1);
    return it->second;
}

/*
 *
 */
void CMasternodesView::Load()
{
    Clear();
    // Load masternodes itself, creating indexes
    db.LoadMasternodes([this] (uint256 & nodeId, CMasternode & node)
    {
        node.counterVotesFrom = 0;
        node.counterVotesAgainst = 0;
        allNodes.insert(std::make_pair(nodeId, node));
        nodesByOwner.insert(std::make_pair(node.ownerAuthAddress, nodeId));
        nodesByOperator.insert(std::make_pair(node.operatorAuthAddress, nodeId));

        if (node.IsActive())
        {
            activeNodes.insert(nodeId);
        }
    });

    // Load dismiss votes and update voting counters
    db.LoadVotes([this] (uint256 & voteId, CDismissVote & vote)
    {
        votes.insert(std::make_pair(voteId, vote));
        votesFrom.insert(std::make_pair(vote.from, voteId));
        votesAgainst.insert(std::make_pair(vote.against, voteId));

        if (vote.IsActive())
        {
            // Assumed that node exists
            assert(HasMasternode(vote.from));
            assert(HasMasternode(vote.against));
            ++allNodes[vote.from].counterVotesFrom;
            ++allNodes[vote.against].counterVotesAgainst;
        }
    });

    // Load undo information
    db.LoadUndo([this] (uint256 & txid, uint256 & affectedItem, char undoType)
    {
        txsUndo.insert(std::make_pair(txid, std::make_pair(affectedItem, static_cast<MasternodesTxType>(undoType))));
    });
}

bool CMasternodesView::OnCollateralSpent(uint256 const & nodeId, uint256 const & txid, uint input, int height)
{
    // Assumed, that node exists
    CMasternode & node = allNodes[nodeId];
    assert(node.collateralSpentTx != uint256());

    if (!currentBatch)
    {
        currentBatch.reset(new CDBBatch(db));
    }

    if (node.IsActive())
    {
        // Remove masternode from active set
        activeNodes.erase(nodeId);

        // Check, deactivate and recalc votes 'from' us
        auto range = votesFrom.equal_range(nodeId);
        for (auto it = range.first; it != range.second; ++it)
        {
            // it->first == nodeId, it->second == voteId
            CDismissVote & vote = votes[it->second];
            if (vote.IsActive())
            {
                vote.disabledByTx = txid;
                vote.deadSinceHeight = height;
                --node.counterVotesFrom;
                --allNodes[vote.against].counterVotesAgainst; // important, was skipped first time!

                db.WriteVote(it->second, vote, *currentBatch);
            }
        }
    }
    // Check, deactivate and recalc votes 'against' us (votes "against us" can exist even if we're not activated yet)
    {
        auto range = votesAgainst.equal_range(nodeId);
        for (auto it = range.first; it != range.second; ++it)
        {
            // it->first == nodeId, it->second == voteId
            CDismissVote & vote = votes[it->second];
            if (vote.IsActive())
            {
                vote.disabledByTx = txid;
                vote.deadSinceHeight = height;
                --node.counterVotesAgainst;
                --allNodes[vote.from].counterVotesFrom; // important, was skipped first time!

                db.WriteVote(it->second, vote, *currentBatch);
            }
        }
    }
    // Like a checksum, count that node.counterVotesFrom == node.counterVotesAgainst == 0 !!!
    assert (node.counterVotesFrom == 0);
    assert (node.counterVotesAgainst == 0);

    node.collateralSpentTx = txid;
    if (node.deadSinceHeight == -1)
    {
        node.deadSinceHeight = height;
        /// @todo @mn update prune index
//        batch->Write(make_pair(DB_MASTERNODESPRUNEINDEX, make_pair(node.deadSinceHeight, txid)), ' ');
    }

    txsUndo.insert(std::make_pair(txid, std::make_pair(nodeId, MasternodesTxType::CollateralSpent)));

    db.WriteUndo(txid, nodeId, static_cast<char>(MasternodesTxType::CollateralSpent), *currentBatch);
    db.WriteMasternode(nodeId, node, *currentBatch);
    return true;
}

bool CMasternodesView::OnMasternodeAnnounce(uint256 const & nodeId, CMasternode const & node)
{
    // Check, that there in no MN with such 'ownerAuthAddress' or 'operatorAuthAddress'
    if (HasMasternode(nodeId) ||
            nodesByOwner.find(node.ownerAuthAddress) != nodesByOwner.end() ||
            nodesByOperator.find(node.operatorAuthAddress) != nodesByOperator.end())
    {
        return false;
    }
    if (!currentBatch)
    {
        currentBatch.reset(new CDBBatch(db));
    }
    allNodes.insert(std::make_pair(nodeId, node));
    nodesByOwner.insert(std::make_pair(node.ownerAuthAddress, nodeId));
    nodesByOperator.insert(std::make_pair(node.operatorAuthAddress, nodeId));

    /// @todo @mn maybe unnesessary undo for announce (it can be recognized by simple check in main masternode index)
    txsUndo.insert(std::make_pair(nodeId, std::make_pair(nodeId, MasternodesTxType::AnnounceMasternode)));

    db.WriteUndo(nodeId, nodeId, static_cast<char>(MasternodesTxType::AnnounceMasternode), *currentBatch);
    db.WriteMasternode(nodeId, node, *currentBatch);
    return true;
}

bool CMasternodesView::OnMasternodeActivate(uint256 const & txid, uint256 const & nodeId, CKeyID const & operatorId, int height)
{
    // Check, that MN was announced
    auto it = nodesByOperator.find(operatorId);
    if (it == nodesByOperator.end() || it->second != nodeId)
    {
        return false;
    }
    // Assumed now, that node exists and consistent with 'nodesByOperator' index
    CMasternode & node = allNodes[nodeId];
    // Checks that MN was not activated nor spent nor finalized (voting) yet
    if (node.activationTx != uint256() || node.collateralSpentTx != uint256() || node.dismissFinalizedTx != uint256() || node.minActivationHeight < height)
    {
        return false;
    }
    if (!currentBatch)
    {
        currentBatch.reset(new CDBBatch(db));
    }
    node.activationTx = txid;
    activeNodes.insert(nodeId);

    txsUndo.insert(std::make_pair(txid, std::make_pair(nodeId, MasternodesTxType::ActivateMasternode)));

    db.WriteUndo(txid, nodeId, static_cast<char>(MasternodesTxType::ActivateMasternode), *currentBatch);
    db.WriteMasternode(nodeId, node, *currentBatch);
    return true;
}

bool CMasternodesView::OnDismissVote(uint256 const & txid, CDismissVote const & vote, CKeyID const & operatorId)
{
    // Checks if:
    //      MN with operator (from) exists and active
    //      MN 'against' exists and active
    //      MN 'from' counter is less than...X
    //      vote with pair 'from'+'against' not exists, or exists but deactivated
    // Then, if all is OK, add vote and increment counters
    // Save
    // (we can get 'active' status just by searching in 'activeNodes' instead of .IsActive())
    auto itFrom = nodesByOperator.find(operatorId);
    if (itFrom == nodesByOperator.end() || allNodes[itFrom->second].IsActive() == false)
    {
        return false;
    }
    uint256 const & idNodeFrom = itFrom->second;

    auto itAgainst = allNodes.find(vote.against);
    if (itAgainst == allNodes.end() || itAgainst->second.IsActive() == false)
    {
        return false;
    }
    CMasternode & nodeFrom = allNodes[idNodeFrom];
    CMasternode & nodeAgainst = itAgainst->second;
    if (nodeFrom.counterVotesFrom >= MAX_DISMISS_VOTES_PER_MN)
    {
        return false;
    }

    // Passerby, remember! Every REAL vote (in 'votes' map) generates 2 indexes.
    // So, when recall (not here, but somewhere), we should remove 2 indexes but only one real vote (i was disappointed with that)
    // I dont want to create ONE MORE index like pair<voteFrom, voteAgainst>->vote, so just iterate existent
    auto range = votesFrom.equal_range(idNodeFrom);
    for (auto it = range.first; it != range.second; ++it)
    {
        // it->first == nodeId, it->second == voteId
        CDismissVote const & tmp = votes[it->second];
        if (tmp.against == vote.against && tmp.IsActive())
        {
            return false;
        }
    }

    if (!currentBatch)
    {
        currentBatch.reset(new CDBBatch(db));
    }
    CDismissVote copy(vote);
    copy.from = idNodeFrom;

    // Updating indexes
    votes.insert(std::make_pair(txid, copy));
    votesFrom.insert(std::make_pair(copy.from, txid));
    votesAgainst.insert(std::make_pair(copy.against, txid));

    // Updating counters
    ++nodeFrom.counterVotesFrom;
    ++nodeAgainst.counterVotesAgainst;

    txsUndo.insert(std::make_pair(txid, std::make_pair(txid, MasternodesTxType::DismissVote)));

    db.WriteUndo(txid, txid, static_cast<char>(MasternodesTxType::DismissVote), *currentBatch);
    db.WriteVote(txid, copy, *currentBatch);
    // we don't write any nodes here, cause only their counters affected
    return true;
}

bool CMasternodesView::OnDismissVoteRecall(uint256 const & txid, uint256 const & against, CKeyID const & operatorId)
{
    // I think we don't need extra checks here (MN active, from and against - if one of MN deactivated - votes was deactivated too). Just checks for active vote
    auto itFrom = nodesByOperator.find(operatorId);
    if (itFrom == nodesByOperator.end() || allNodes[itFrom->second].IsActive() == false)
    {
        return false;
    }
    uint256 const & idNodeFrom = itFrom->second;


    /// WIP
    ///
    ///
    return true;
}

void CMasternodesView::WriteBatch()
{
    if (currentBatch)
    {
        db.WriteBatch(*currentBatch);
        currentBatch.reset();
    }
}

void CMasternodesView::Clear()
{
    allNodes.clear();
    activeNodes.clear();
    nodesByOwner.clear();
    nodesByOperator.clear();

    votes.clear();
    votesFrom.clear();
    votesAgainst.clear();

    txsUndo.clear();
}
