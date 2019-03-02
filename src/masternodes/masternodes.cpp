// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternodes.h"

#include "arith_uint256.h"
#include "chainparams.h"
#include "key_io.h"
#include "primitives/block.h"
#include "script/standard.h"
#include "txdb.h"
#include "util.h"

#include <algorithm>
#include <functional>


static const std::map<char, MasternodesTxType> MasternodesTxTypeToCode =
{
    {'a', MasternodesTxType::AnnounceMasternode },
    {'A', MasternodesTxType::ActivateMasternode },
    {'O', MasternodesTxType::SetOperatorReward },
    {'V', MasternodesTxType::DismissVote },
    {'v', MasternodesTxType::DismissVoteRecall },
    {'F', MasternodesTxType::FinalizeDismissVoting }
    // Without CollateralSpent
};

extern CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams); // in main.cpp

int GetMnActivationDelay()
{
    static const int MN_ACTIVATION_DELAY = 100;
    if (Params().NetworkIDString() == "regtest")
    {
        return 10;
    }
    return MN_ACTIVATION_DELAY ;
}

CAmount GetMnCollateralAmount()
{
    static const CAmount MN_COLLATERAL_AMOUNT = 1000000 * COIN;

    if (Params().NetworkIDString() == "regtest")
    {
        return 10 * COIN;
    }
    return MN_COLLATERAL_AMOUNT;
}

CAmount GetMnAnnouncementFee(CAmount const & blockSubsidy, int height, size_t activeMasternodesNum)
{
    Consensus::Params const & consensus = Params().GetConsensus();
    size_t const dPosTeamSize = consensus.dpos.nTeamSize;

    const int nMinBlocksOfIncome = consensus.nDposMinPeriodOfIncome / consensus.nPowTargetSpacing;
    const int nMaxBlocksOfIncome = consensus.nDposMaxPeriodOfIncome / consensus.nPowTargetSpacing;
    const int nGrowingPeriodBlocks = consensus.nDposGrowingPeriod / consensus.nPowTargetSpacing;

    activeMasternodesNum = activeMasternodesNum < dPosTeamSize ? dPosTeamSize : std::max(dPosTeamSize, activeMasternodesNum);

    const CAmount masternodesBlockReward = blockSubsidy * GetDposBlockSubsidyRatio() / MN_BASERATIO;
    const CAmount masternodeIncome = masternodesBlockReward / activeMasternodesNum;

    const CAmount minAnnouncementFee = masternodeIncome * nMinBlocksOfIncome;
    const CAmount maxAnnouncementFee = masternodeIncome * nMaxBlocksOfIncome;

    const CAmount feePerBlock = (maxAnnouncementFee - minAnnouncementFee) / nGrowingPeriodBlocks;
    const int sapling_activation_block_number = consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight;
    const int blocksSinceSapling = height - sapling_activation_block_number;

    if (height < sapling_activation_block_number)
    {
        return minAnnouncementFee;
    }
    return std::min(maxAnnouncementFee, minAnnouncementFee + feePerBlock * blocksSinceSapling);
}

int32_t GetDposBlockSubsidyRatio()
{
    return MN_BASERATIO / 2;
}

void CMasternode::FromTx(CTransaction const & tx, int heightIn, std::vector<unsigned char> const & metadata)
{
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> name;
    ss >> ownerAuthAddress;
    ss >> operatorAuthAddress;
    ss >> *(CScriptBase*)(&ownerRewardAddress);
    ss >> *(CScriptBase*)(&operatorRewardAddress);
    ss >> operatorRewardRatio;

    height = heightIn;
    // minActivationHeight should be set outside cause depends from current active count
    minActivationHeight = -1;
    activationHeight = -1;
    deadSinceHeight = -1;

    activationTx = uint256();
    collateralSpentTx = uint256();
    dismissFinalizedTx = uint256();

    counterVotesFrom = 0;
    counterVotesAgainst = 0;
}

std::string CMasternode::GetHumanReadableStatus() const
{
    std::string status;
    if (IsActive())
    {
        return "active";
    }
    status += (activationTx == uint256()) ? "announced" : "activated";
    if (collateralSpentTx != uint256())
    {
        status += ", resigned";
    }
    if (dismissFinalizedTx != uint256())
    {
        status += ", dismissed";
    }
    return status;
}

bool operator==(CMasternode const & a, CMasternode const & b)
{
    return (a.name == b.name &&
            a.ownerAuthAddress == b.ownerAuthAddress &&
            a.operatorAuthAddress == b.operatorAuthAddress &&
            a.ownerRewardAddress == b.ownerRewardAddress &&
            a.operatorRewardAddress == b.operatorRewardAddress &&
            a.operatorRewardRatio == b.operatorRewardRatio &&
            a.height == b.height &&
            a.minActivationHeight == b.minActivationHeight &&
            a.activationHeight == b.activationHeight &&
            a.deadSinceHeight == b.deadSinceHeight &&
            a.activationTx == b.activationTx &&
            a.collateralSpentTx == b.collateralSpentTx &&
            a.dismissFinalizedTx == b.dismissFinalizedTx &&
            a.counterVotesFrom == b.counterVotesFrom &&
            a.counterVotesAgainst == b.counterVotesAgainst
            );
}

bool operator!=(CMasternode const & a, CMasternode const & b)
{
    return !(a == b);
}


void CDismissVote::FromTx(CTransaction const & tx, std::vector<unsigned char> const & metadata)
{
    from = uint256();
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> against;
    ss >> reasonCode;
    ss >> reasonDescription;
    deadSinceHeight = -1;
    disabledByTx = uint256();
}

bool operator==(CDismissVote const & a, CDismissVote const & b)
{
    return (a.from == b.from &&
            a.against == b.against &&
            a.reasonCode == b.reasonCode &&
            a.reasonDescription == b.reasonDescription &&
            a.deadSinceHeight == b.deadSinceHeight &&
            a.disabledByTx == b.disabledByTx
            );
}

bool operator!=(const CDismissVote & a, const CDismissVote & b)
{
    return !(a == b);
}


CMasternodesView::CMasternodesView(CMasternodesView const & other)
    : db(other.db)
    , allNodes(other.allNodes)
    , activeNodes(other.activeNodes)
    , nodesByOwner(other.nodesByOwner)
    , nodesByOperator(other.nodesByOperator)
    , votes(other.votes)
    , votesFrom(other.votesFrom)
    , votesAgainst(other.votesAgainst)
    , txsUndo(other.txsUndo)
    , operatorUndo(other.operatorUndo)
{
}

/*
 * Searching MN index 'nodesByOwner' or 'nodesByOperator' for given 'auth' key
 */
boost::optional<CMasternodesByAuth::const_iterator> CMasternodesView::ExistMasternode(CMasternodesView::AuthIndex where, CKeyID const & auth) const
{
    CMasternodesByAuth const & index = (where == AuthIndex::ByOwner) ? nodesByOwner : nodesByOperator;
    auto it = index.find(auth);
    if (it == index.end())
    {
        return {};
    }
    return {it};
}

/*
 * Searching all masternodes for given 'id'
 */
CMasternode const * CMasternodesView::ExistMasternode(uint256 const & id) const
{
    CMasternodes::const_iterator it = allNodes.find(id);
    if (it == allNodes.end())
    {
        return nullptr;
    }
    return &it->second;
}

/*
 * Loads all data from DB, creates indexes, calculates voting counters
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

        if (vote.IsActive())
        {
            // Indexing only active votes
            votesFrom.insert(std::make_pair(vote.from, voteId));
            votesAgainst.insert(std::make_pair(vote.against, voteId));

            // Assumed that node exists
            ++allNodes.at(vote.from).counterVotesFrom;
            ++allNodes.at(vote.against).counterVotesAgainst;
        }
    });

    // Load undo information
    db.LoadUndo([this] (uint256 & txid, uint256 & affectedItem, char undoType)
    {
        txsUndo.insert(std::make_pair(txid, std::make_pair(affectedItem, static_cast<MasternodesTxType>(undoType))));

        // There is the second way: load all 'operator undo' in different loop, but i think here is more "consistent"
        if (undoType == static_cast<char>(MasternodesTxType::SetOperatorReward))
        {
            COperatorUndoRec rec;
            db.ReadOperatorUndo(txid, rec);
            operatorUndo.insert(std::make_pair(txid, rec));
        }
    });

    /// @todo @mn Where is the correct point of Team loading?
//    db.ReadTeam(chainActive.Height());
}

/*
 * Private. Deactivates vote, decrement counters, save state
 * Nothing checks cause private.
*/
void CMasternodesView::DeactivateVote(uint256 const & voteId, uint256 const & txid, int height)
{
    CDismissVote & vote = votes.at(voteId);

    vote.disabledByTx = txid;
    vote.deadSinceHeight = height;
    --allNodes.at(vote.from).counterVotesFrom;
    --allNodes.at(vote.against).counterVotesAgainst;

    txsUndo.insert(std::make_pair(txid, std::make_pair(voteId, MasternodesTxType::DismissVoteRecall)));
    db.WriteUndo(txid, voteId, static_cast<char>(MasternodesTxType::DismissVoteRecall));
    db.WriteVote(voteId, vote);
}

/*
 * Private. Deactivates votes (from active node, against any), recalculates all counters
 * Used in two plases: on deactivation by collateral spent and on finalize voting.
 * Nothing checks cause private.
*/
void CMasternodesView::DeactivateVotesFor(uint256 const & nodeId, uint256 const & txid, int height)
{
    CMasternode & node = allNodes.at(nodeId);

    if (node.IsActive())
    {
        // Check, deactivate and recalc votes 'from' us (remember, 'votesFrom' and 'votesAgainst' contains only active votes)
        auto const & range = votesFrom.equal_range(nodeId);
        std::for_each(range.first, range.second, [&] (std::pair<uint256 const, uint256> const & it)
        {
            // it.first == nodeId (from), it.second == voteId
            DeactivateVote(it.second, txid, height);
            votesAgainst.erase(votes.at(it.second).against);
        });
        votesFrom.erase(range.first, range.second);
    }
    // Check, deactivate and recalc votes 'against' us (votes "against us" can exist even if we're not activated yet)
    {
        auto const & range = votesAgainst.equal_range(nodeId);
        std::for_each(range.first, range.second, [&] (std::pair<uint256 const, uint256> const & it)
        {
            // it->first == nodeId (against), it->second == voteId
            DeactivateVote(it.second, txid, height);
            votesFrom.erase(votes.at(it.second).from);
        });
        votesAgainst.erase(range.first, range.second);
    }
    // Like a checksum, count that node.counterVotesFrom == node.counterVotesAgainst == 0 !!!
    assert (node.counterVotesFrom == 0);
    assert (node.counterVotesAgainst == 0);
}

bool CMasternodesView::OnCollateralSpent(uint256 const & nodeId, uint256 const & txid, uint32_t input, int height)
{
    // Assumed, that node exists
    CMasternode & node = allNodes.at(nodeId);
    if (node.collateralSpentTx != uint256())
    {
        return false;
    }
    if (node.IsActive())
    {
        // Remove masternode from active set
        activeNodes.erase(nodeId);
    }

    DeactivateVotesFor(nodeId, txid, height);

    node.collateralSpentTx = txid;
    if (node.deadSinceHeight == -1)
    {
        node.deadSinceHeight = height;
        /// @todo @mn update prune index
//        batch->Write(make_pair(DB_MASTERNODESPRUNEINDEX, make_pair(node.deadSinceHeight, txid)), ' ');
    }

    txsUndo.insert(std::make_pair(txid, std::make_pair(nodeId, MasternodesTxType::CollateralSpent)));

    db.WriteUndo(txid, nodeId, static_cast<char>(MasternodesTxType::CollateralSpent));
    db.WriteMasternode(nodeId, node);
    return true;
}

bool CMasternodesView::OnMasternodeAnnounce(uint256 const & nodeId, CMasternode const & node)
{
    // Check, that there in no MN with such 'ownerAuthAddress' or 'operatorAuthAddress'
    if (ExistMasternode(nodeId) ||
            nodesByOwner.find(node.ownerAuthAddress) != nodesByOwner.end() ||
            nodesByOwner.find(node.operatorAuthAddress) != nodesByOwner.end() ||
            nodesByOperator.find(node.ownerAuthAddress) != nodesByOperator.end() ||
            nodesByOperator.find(node.operatorAuthAddress) != nodesByOperator.end())
    {
        return false;
    }

    allNodes.insert(std::make_pair(nodeId, node));
    nodesByOwner.insert(std::make_pair(node.ownerAuthAddress, nodeId));
    nodesByOperator.insert(std::make_pair(node.operatorAuthAddress, nodeId));

    txsUndo.insert(std::make_pair(nodeId, std::make_pair(nodeId, MasternodesTxType::AnnounceMasternode)));

    db.WriteUndo(nodeId, nodeId, static_cast<char>(MasternodesTxType::AnnounceMasternode));
    db.WriteMasternode(nodeId, node);
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
    CMasternode & node = allNodes.at(nodeId);
    // Checks that MN was not activated nor spent nor finalized (voting) yet
    // We can check only 'deadSinceHeight != -1' so it must be consistent with 'collateralSpentTx' and 'dismissFinalizedTx'
    if (node.activationTx != uint256() || node.deadSinceHeight != -1 || node.minActivationHeight > height)
    {
        return false;
    }

    node.activationTx = txid;
    node.activationHeight = height;
    activeNodes.insert(nodeId);

    txsUndo.insert(std::make_pair(txid, std::make_pair(nodeId, MasternodesTxType::ActivateMasternode)));

    db.WriteUndo(txid, nodeId, static_cast<char>(MasternodesTxType::ActivateMasternode));
    db.WriteMasternode(nodeId, node);
    return true;
}

bool CMasternodesView::OnDismissVote(uint256 const & txid, CDismissVote const & vote, CKeyID const & operatorId)
{
    // Checks if:
    //      MN with operator (from) exists and active
    //      MN 'against' exists and not spent nor finalized (but may be not activated yet)
    //      MN 'from' counter is less than...X
    //      vote with pair 'from'+'against' not exists, or exists but deactivated
    // Then, if all is OK, add vote and increment counters
    // Save
    // (we can get 'active' status just by searching in 'activeNodes' instead of .IsActive())
    auto const & itFrom = nodesByOperator.find(operatorId);
    if (itFrom == nodesByOperator.end() || allNodes.at(itFrom->second).IsActive() == false)
    {
        return false;
    }
    uint256 const & idNodeFrom = itFrom->second;

    auto itAgainst = allNodes.find(vote.against);
    // We can check only by 'deadSince != -1' so it must be consistent with 'collateralSpentTx' and 'dismissFinalizedTx'
    if (itAgainst == allNodes.end() || itAgainst->second.deadSinceHeight != -1)
    {
        return false;
    }
    CMasternode & nodeFrom = allNodes.at(idNodeFrom);
    CMasternode & nodeAgainst = itAgainst->second;
    if (nodeFrom.counterVotesFrom >= MAX_DISMISS_VOTES_PER_MN)
    {
        return false;
    }

    if (ExistActiveVoteIndex(VoteIndex::From, idNodeFrom, vote.against)) // no need to check second index cause they are consistent
    {
        return false;
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

    db.WriteUndo(txid, txid, static_cast<char>(MasternodesTxType::DismissVote));
    db.WriteVote(txid, copy);
    // we don't write any nodes here, cause only their counters affected
    return true;
}


/*
 * Search in active vote index for pair 'from' 'against'
 * returns optional iterator
*/
boost::optional<CDismissVotesIndex::const_iterator>
CMasternodesView::ExistActiveVoteIndex(VoteIndex where, uint256 const & from, uint256 const & against) const
{
    typedef std::pair<uint256 const, uint256> const & TPairConstRef;
    typedef std::function<bool(TPairConstRef)> TPredicate;
    TPredicate const & isEqualAgainst = [&against, this] (TPairConstRef pair) { return votes.at(pair.second).against == against; };
    TPredicate const & isEqualFrom    = [&from,    this] (TPairConstRef pair) { return votes.at(pair.second).from == from; };

    auto const & range = (where == VoteIndex::From) ? votesFrom.equal_range(from) : votesAgainst.equal_range(against);
    CDismissVotesIndex::const_iterator it = std::find_if(range.first, range.second, where == VoteIndex::From ? isEqualAgainst : isEqualFrom);
    if (it == range.second)
    {
        return {};
    }
    return {it};
}

bool CMasternodesView::OnDismissVoteRecall(uint256 const & txid, uint256 const & against, CKeyID const & operatorId, int height)
{
    // I think we don't need extra checks here (MN active, from and against - if one of MN deactivated - votes was deactivated too). Just checks for active vote
    auto itFrom = nodesByOperator.find(operatorId);
    if (itFrom == nodesByOperator.end() || allNodes.at(itFrom->second).IsActive() == false)
    {
        return false;
    }
    uint256 const & idNodeFrom = itFrom->second;

    // We forced to iterate through all range to find target vote.
    // Unfortunately, even extra index wouldn't help us, cause we whatever need to erase recalled vote from both
    // Remember! Every REAL and ACTIVE vote (in 'votes' map) has 2 indexes.
    // So, when recall, we should remove 2 indexes but only one real vote
    auto const optionalIt = ExistActiveVoteIndex(VoteIndex::From, idNodeFrom, against);
    if (!optionalIt)
    {
        return false;
    }

    uint256 const & voteId = (*optionalIt)->second;
    CDismissVote & vote = votes.at(voteId);

    // Here is real job: modify and write vote, remove active indexes, write undo
    vote.disabledByTx = txid;
    vote.deadSinceHeight = height;

    --allNodes.at(idNodeFrom).counterVotesFrom;
    --allNodes.at(against).counterVotesAgainst; // important, was skipped first time!

    txsUndo.insert(std::make_pair(txid, std::make_pair(voteId, MasternodesTxType::DismissVoteRecall)));

    db.WriteUndo(txid, voteId, static_cast<char>(MasternodesTxType::DismissVoteRecall));
    db.WriteVote(voteId, vote);

    votesFrom.erase(*optionalIt);

    // Finally, remove link from second index. It SHOULD be there.
    {
        auto const optionalIt = ExistActiveVoteIndex(VoteIndex::Against, idNodeFrom, against);
        assert(optionalIt);
        votesAgainst.erase(*optionalIt);
    }
    return true;
}

bool CMasternodesView::OnFinalizeDismissVoting(uint256 const & txid, uint256 const & nodeId, int height)
{
    auto it = allNodes.find(nodeId);
    // We can check only 'deadSinceHeight != -1' so it must be consistent with 'collateralSpentTx' and 'dismissFinalizedTx'
    // It will not be accepted if collateral was spent, cause votes were not accepted too (collateral spent is absolute blocking condition)
    if (it == allNodes.end() || it->second.counterVotesAgainst < GetMinDismissingQuorum() || it->second.deadSinceHeight != -1)
    {
        return false;
    }

    CMasternode & node = it->second;
    if (node.IsActive())
    {
        // Remove masternode from active set
        activeNodes.erase(nodeId);
    }

    DeactivateVotesFor(nodeId, txid, height);

    node.dismissFinalizedTx = txid;
    if (node.deadSinceHeight == -1)
    {
        node.deadSinceHeight = height;
        /// @todo @mn update prune index
//        batch->Write(make_pair(DB_MASTERNODESPRUNEINDEX, make_pair(node.deadSinceHeight, txid)), ' ');
    }

    txsUndo.insert(std::make_pair(txid, std::make_pair(nodeId, MasternodesTxType::FinalizeDismissVoting)));

    db.WriteUndo(txid, nodeId, static_cast<char>(MasternodesTxType::FinalizeDismissVoting));
    db.WriteMasternode(nodeId, node);

    return true;
}

bool CMasternodesView::OnSetOperatorReward(uint256 const & txid, CKeyID const & ownerId,
                                           CKeyID const & newOperatorAuthAddress, CScript const & newOperatorRewardAddress, CAmount newOperatorRewardRatio, int height)
{
    // Check, that MN was announced
    auto it = nodesByOwner.find(ownerId);
    if (it == nodesByOwner.end())
    {
        return false;
    }
    // Assumed now, that node exists and consistent with 'nodesByOperator' index
    uint256 const & nodeId = it->second;
    CMasternode & node = allNodes.at(nodeId);

    if (nodesByOwner.find(newOperatorAuthAddress) != nodesByOwner.end() ||
       (nodesByOperator.find(newOperatorAuthAddress) != nodesByOperator.end() && node.operatorAuthAddress != newOperatorAuthAddress))
    {
        return false;
    }

    nodesByOperator.erase(node.operatorAuthAddress);
    nodesByOperator.insert(std::make_pair(newOperatorAuthAddress, nodeId));

    COperatorUndoRec operatorUndoRec{ node.operatorAuthAddress, node.operatorRewardAddress, node.operatorRewardRatio };
    node.operatorAuthAddress = newOperatorAuthAddress;
    node.operatorRewardAddress = newOperatorRewardAddress;
    node.operatorRewardRatio = newOperatorRewardRatio;

    txsUndo.insert(std::make_pair(txid, std::make_pair(nodeId, MasternodesTxType::SetOperatorReward)));
    operatorUndo.insert(std::make_pair(txid, operatorUndoRec));

    db.WriteUndo(txid, nodeId, static_cast<char>(MasternodesTxType::SetOperatorReward));
    db.WriteOperatorUndo(txid, operatorUndoRec);

    db.WriteMasternode(nodeId, node);
    return true;
}

//bool CMasternodesView::HasUndo(uint256 const & txid) const
//{
//    return txsUndo.find(txid) != txsUndo.end();
//}

bool CMasternodesView::OnUndo(uint256 const & txid)
{
    auto itUndo = txsUndo.find(txid);
    if (itUndo == txsUndo.end())
    {
        return false;
    }

    // *** Note: only one iteration except 'CollateralSpent' and 'FinalizeDismissVoting' cause additional votes restoration
    while (itUndo != txsUndo.end() && itUndo->first == txid)
    {
        //  *itUndo == std::pair<uint256 txid, std::pair<uint256 affected_object_id, MasternodesTxType> >
        uint256 const & id = itUndo->second.first;
        MasternodesTxType txType = itUndo->second.second;
        switch (txType)
        {
            case MasternodesTxType::CollateralSpent:    // notify that all deactivated child votes will be restored by DismissVoteRecall additional undo
            {
                CMasternode & node = allNodes.at(id);

                node.collateralSpentTx = uint256();
                // Check if 'spent' was an only reason to deactivate
                if (node.dismissFinalizedTx == uint256())
                {
                    node.deadSinceHeight = -1;
                    activeNodes.insert(id);
                }
                db.WriteMasternode(id, node);
            }
            break;
            case MasternodesTxType::AnnounceMasternode:
            {
                CMasternode & node = allNodes.at(id);

                nodesByOwner.erase(node.ownerAuthAddress);
                nodesByOperator.erase(node.operatorAuthAddress);
                allNodes.erase(id);

                db.EraseMasternode(id);
            }
            break;
            case MasternodesTxType::ActivateMasternode:
            {
                CMasternode & node = allNodes.at(id);

                node.activationTx = uint256();
                node.activationHeight = -1;

                activeNodes.erase(id);

                db.WriteMasternode(id, node);
            }
            break;
            case MasternodesTxType::SetOperatorReward:
            {
                CMasternode & node = allNodes.at(id);

                nodesByOperator.erase(node.operatorAuthAddress);

                auto const & rec = operatorUndo.at(txid);
                node.operatorAuthAddress = rec.operatorAuthAddress;
                node.operatorRewardAddress = rec.operatorRewardAddress;
                node.operatorRewardRatio = rec.operatorRewardRatio;

                nodesByOperator.insert(std::make_pair(node.operatorAuthAddress, id));

                operatorUndo.erase(txid);
                db.EraseOperatorUndo(txid);

                db.WriteMasternode(id, node);
            }
            break;
            case MasternodesTxType::DismissVote:
            {
                CDismissVote & vote = votes.at(id);

                // Updating counters first
                --allNodes.at(vote.from).counterVotesFrom;
                --allNodes.at(vote.against).counterVotesAgainst;

                votesFrom.erase(vote.from);
                votesAgainst.erase(vote.against);
                votes.erase(id);    // last!

                db.EraseVote(id);
            }
            break;
            case MasternodesTxType::DismissVoteRecall:
            {
                CDismissVote & vote = votes.at(id);

                ++allNodes.at(vote.from).counterVotesFrom;
                ++allNodes.at(vote.against).counterVotesAgainst;

                votesFrom.insert(std::make_pair(vote.from, id));
                votesAgainst.insert(std::make_pair(vote.against, id));

                vote.disabledByTx = uint256();
                vote.deadSinceHeight = -1;

                db.WriteVote(id, vote);
            }
            break;
            case MasternodesTxType::FinalizeDismissVoting: // notify that all deactivated child votes will be restored by DismissVoteRecall additional undo
            {
                CMasternode & node = allNodes.at(id);

                node.dismissFinalizedTx = uint256();
                if (node.collateralSpentTx == uint256())
                {
                    node.deadSinceHeight = -1;
                }
                if (node.IsActive())
                {
                    activeNodes.insert(id);
                }
                db.WriteMasternode(id, node);
            }
            break;

            default:
                break;
        }
        db.EraseUndo(txid, id); // erase db first! then map (cause iterator)!
        itUndo = txsUndo.erase(itUndo); // instead ++itUndo;
    }
    return true;
}

bool CMasternodesView::IsTeamMember(int height, CKeyID const & operatorAuth) const
{
    CTeam team = ReadDposTeam(height);
    for (auto const & member : team)
    {
        if (member.second.second == operatorAuth)
            return true;
    }
    return false;
}

struct KeyLess
{
    template<typename KeyType>
    static KeyType getKey(KeyType const & k)
    {
        return k;
    }

    template<typename KeyType, typename ValueType>
    static KeyType getKey(std::pair<KeyType const, ValueType> const & p)
    {
        return p.first;
    }

    template<typename L, typename R>
    bool operator()(L const & l, R const & r) const
    {
        return getKey(l) < getKey(r);
    }
};

CTeam CMasternodesView::CalcNextDposTeam(CActiveMasternodes const & activeNodes, CMasternodes const & allNodes, uint256 const & blockHash, int height)
{
    CTeam team = ReadDposTeam(height);
    size_t const dPosTeamSize = Params().GetConsensus().dpos.nTeamSize;

    assert(team.size() <= dPosTeamSize);
    // erase oldest member
    if (team.size() == dPosTeamSize)
    {
        auto oldest_it = std::max_element(team.begin(), team.end(), [](CTeam::value_type const & lhs, CTeam::value_type const & rhs)
        {
            if (lhs.second == rhs.second)
            {
                return UintToArith256(lhs.first) < UintToArith256(rhs.first);
            }
            return lhs.second < rhs.second;
        });
        if (oldest_it != team.end())
        {
            team.erase(oldest_it);
        }
    }
    // erase dismissed/resigned members
    for(auto it = team.begin(); it != team.end();)
    {
        if(activeNodes.find(it->first) == activeNodes.end())
        {
            it = team.erase(it);
        }
        else ++it;
    }

    // get active masternodes which are not included in the current team vector;
    std::vector<uint256> mayJoin;
    std::set_difference(activeNodes.begin(), activeNodes.end(), team.begin(), team.end(), std::inserter(mayJoin, mayJoin.begin()), KeyLess());

    // sort by selectors
    std::sort(mayJoin.begin(), mayJoin.end(), [ &blockHash ](uint256 const & lhs, uint256 const & rhs)
    {
        CDataStream lhs_ss(SER_GETHASH, 0);
        lhs_ss << lhs << blockHash;
        arith_uint256 lhs_selector = UintToArith256(Hash(lhs_ss.begin(), lhs_ss.end())); // '<' operators of uint256 and uint256_arith are different

        CDataStream rhs_ss(SER_GETHASH, 0);
        rhs_ss << rhs << blockHash;
        arith_uint256 rhs_selector = UintToArith256(Hash(rhs_ss.begin(), rhs_ss.end())); // '<' operators of uint256 and uint256_arith are different

        return ArithToUint256(lhs_selector) < ArithToUint256(rhs_selector);
    });

    // calc new members
    const size_t freeSlots = dPosTeamSize - team.size();
    const size_t toJoin = std::min(mayJoin.size(), freeSlots);

    for (size_t i = 0; i < toJoin; ++i)
    {
        team.insert(std::make_pair(mayJoin[i], std::make_pair(height, allNodes.at(mayJoin[i]).operatorAuthAddress)));
    }

    if (!db.WriteTeam(height+1, team))
    {
        throw std::runtime_error("Masternodes database is corrupted (writing dPoS team)! Please restart with -reindex to recover.");
    }
    return team;
}

CTeam CMasternodesView::ReadDposTeam(int height) const
{
    CTeam team;
    if (!db.ReadTeam(height, team))
    {
        throw std::runtime_error("Masternodes database is corrupted (reading dPoS team)! Please restart with -reindex to recover.");
    }
    return team;
}

/*
 *
*/
extern CFeeRate minRelayTxFee;
std::pair<std::vector<CTxOut>, CAmount> CMasternodesView::CalcDposTeamReward(CAmount totalBlockSubsidy, CAmount dPosTransactionsFee, int height) const
{
    std::vector<CTxOut> result;
    CTeam const team = ReadDposTeam(height - 1);
    bool const fDposActive = team.size() == Params().GetConsensus().dpos.nTeamSize;
    if (!fDposActive)
    {
        return {result, 0};
    }

    CAmount const dposReward_one = ((totalBlockSubsidy * GetDposBlockSubsidyRatio()) / MN_BASERATIO) / team.size();
    CAmount dposReward = 0;

    for (auto it = team.begin(); it != team.end(); ++it)
    {
        // it->first == nodeId, it->second == joinHeight
        uint256 const & nodeId = it->first;
        CMasternode const & node = allNodes.at(nodeId);

        CAmount ownerReward = dposReward_one;
        CAmount operatorReward = ownerReward * node.operatorRewardRatio / MN_BASERATIO;
        ownerReward -= operatorReward;
        operatorReward += dPosTransactionsFee / team.size();

        // Merge outputs this way. Checking equality of scriptPubKeys BEFORE creating couts to avoid situation
        // when scripts are equal but particular amounts are dust!
        if (node.ownerRewardAddress == node.operatorRewardAddress)
        {
            CTxOut out(ownerReward+operatorReward, node.ownerRewardAddress);
            if (!out.IsDust(::minRelayTxFee))
            {
                result.push_back(out);
                dposReward += ownerReward+operatorReward;
            }
        }
        else
        {
            CTxOut outOwner(ownerReward, node.ownerRewardAddress);
            if (!outOwner.IsDust(::minRelayTxFee))
            {
                result.push_back(outOwner);
                dposReward += ownerReward;
            }
            CTxOut outOperator(operatorReward, node.operatorRewardAddress);
            if (!outOperator.IsDust(::minRelayTxFee))
            {
                result.push_back(outOperator);
                dposReward += operatorReward;
            }
        }
    }
    // sorting result by hashes
    std::sort(result.begin(), result.end(), [&](CTxOut const & lhs, CTxOut const & rhs)
    {
        return UintToArith256(lhs.GetHash()) < UintToArith256(rhs.GetHash());
    });
    return {result, dposReward};
}

uint32_t CMasternodesView::GetMinDismissingQuorum()
{
    if (Params().NetworkIDString() != "regtest")
    {
        const uint32_t perc66 = static_cast<uint32_t>((activeNodes.size() * 2) / 3); // 66%
        return std::max(perc66, 32u);
    }
    else
    {
        return  static_cast<uint32_t>(1 + (activeNodes.size() * 2) / 3); // 66% + 1
    }
}

void CMasternodesView::CommitBatch()
{
    db.CommitBatch();
}

void CMasternodesView::DropBatch()
{
    db.DropBatch();
}

boost::optional<CMasternodesView::CMasternodeIDs> CMasternodesView::AmI(AuthIndex where) const
{
    std::string addressBase58 = (where == AuthIndex::ByOperator) ? GetArg("-masternode_operator", "") : GetArg("-masternode_owner", "");
    if (addressBase58 != "")
    {
        CTxDestination dest = DecodeDestination(addressBase58);
        CKeyID const * authAddress = boost::get<CKeyID>(&dest);
        if (authAddress)
        {
            /// @todo refactor to ExistMasternode()
            CMasternodesByAuth const & index = (where == AuthIndex::ByOperator) ? nodesByOperator : nodesByOwner;
            auto const & it = index.find(*authAddress);
            if (it != index.end())
            {
                uint256 const & id = it->second;
                return { CMasternodeIDs {id, allNodes.at(id).operatorAuthAddress, allNodes.at(id).ownerAuthAddress} };
            }
        }
    }
    return {};
}

boost::optional<CMasternodesView::CMasternodeIDs> CMasternodesView::AmIOperator() const
{
    return AmI(AuthIndex::ByOperator);
}

boost::optional<CMasternodesView::CMasternodeIDs> CMasternodesView::AmIOwner() const
{
    return AmI(AuthIndex::ByOwner);
}

boost::optional<CMasternodesView::CMasternodeIDs> CMasternodesView::AmIActiveOperator() const
{
    auto result = AmI(AuthIndex::ByOperator);
    if (result && allNodes.at(result->id).IsActive())
    {
        return result;
    }
    return {};
}

boost::optional<CMasternodesView::CMasternodeIDs> CMasternodesView::AmIActiveOwner() const
{
    auto result = AmI(AuthIndex::ByOwner);
    if (result && allNodes.at(result->id).IsActive())
    {
        return result;
    }
    return {};
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


/*
 * Checks if given tx is probably one of 'MasternodeTx', returns tx type and serialized metadata in 'data'
*/
MasternodesTxType GuessMasternodeTxType(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    assert(tx.vout.size() > 0);
    CScript const & memo = tx.vout[0].scriptPubKey;
    CScript::const_iterator pc = memo.begin();
    opcodetype opcode;
    if (!memo.GetOp(pc, opcode) || opcode != OP_RETURN)
    {
        return MasternodesTxType::None;;
    }
    if (!memo.GetOp(pc, opcode, metadata) ||
            (opcode > OP_PUSHDATA1 &&
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

