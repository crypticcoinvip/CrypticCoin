// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dpos.h"
#include "util.h"
#include "../protocol.h"
#include "../net.h"
#include "../main.h"
#include "../chainparams.h"
#include "../validationinterface.h"
#include "../masternodes/masternodes.h"
#include "../consensus/upgrades.h"
#include "../consensus/validation.h"
#include "../snark/libsnark/common/utils.hpp"
#include <mutex>

namespace {
using LockGuard = std::lock_guard<std::mutex>;
std::mutex mutex_{};
CTransactionVoteTracker* transactionVoteTracker_{nullptr};
CProgenitorVoteTracker* progenitorVoteTrackerInstance_{nullptr};
CProgenitorBlockTracker* progenitorBlockTrackerInstance_{nullptr};
std::array<unsigned char, 16> salt_{0x4D, 0x48, 0x7A, 0x52, 0x5D, 0x4D, 0x37, 0x78, 0x42, 0x36, 0x5B, 0x64, 0x44, 0x79, 0x59, 0x4F};

class ChainListener : public CValidationInterface
{
public:
    std::map<uint256, CTransactionVote> transactionVotes;
    std::map<uint256, CProgenitorVote> progenitorVotes;
    std::map<uint256, CBlock> progenitorBlocks;

protected:
    void UpdatedBlockTip(const CBlockIndex* pindex) override;
    void SyncTransaction(const CTransaction& tx, const CBlock* pblock) override;
} chainListener_;

struct VoteDistribution
{
    std::size_t pro;
    std::size_t contra;
    std::size_t abstinendi;
    std::size_t totus;

    bool checkSufficiency() const;
};

void attachTransactions(CBlock& block)
{
    LOCK(cs_main);
    const int nHeight{chainActive.Tip()->nHeight + 1};
    const int64_t nMedianTimePast{chainActive.Tip()->GetMedianTimePast()};

    for (auto mi{mempool.mapTx.begin()}; mi != mempool.mapTx.end(); ++mi) {
        const CTransaction& tx = mi->GetTx();
        const int64_t nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                                      ? nMedianTimePast
                                      : block.GetBlockTime();

        if (!tx.IsCoinBase() && IsFinalTx(tx, nHeight, nLockTimeCutoff) && !IsExpiredTx(tx, nHeight)) {
            block.vtx.push_back(tx);
        }
    }
}

//CBlock tranformProgenitorBlock(const CBlock& progenitorBlock)
//{
//    CBlock rv{progenitorBlock.GetBlockHeader()};
////    rv.nRoundNumber = progenitorBlock.nRoundNumber;
//    rv.vtx.resize(progenitorBlock.vtx.size());
//    std::copy(progenitorBlock.vtx.begin(), progenitorBlock.vtx.end(), rv.vtx.begin());
//    rv.hashMerkleRoot = rv.BuildMerkleTree();
//    //        attachTransactions(dposBlock);
//    return rv;
//}

std::map<uint256, VoteDistribution> calcTxVoteStats()
{
    std::map<uint256, VoteDistribution> rv{};

    for (const auto& txVote : CTransactionVoteTracker::getInstance().listReceivedVotes()) {
        for (const auto& choice : txVote.choices) {
            VoteDistribution& stats{rv[choice.hash]};

            switch (choice.decision) {
            case CVoteChoice::decisionYes:
                stats.pro++;
                break;
            case CVoteChoice::decisionNo:
                stats.contra++;
                break;
            case CVoteChoice::decisionPass:
                stats.abstinendi++;
                break;
            }
            stats.totus++;
        }
    }
    return rv;
}

std::size_t getActiveMasternodeCount()
{
    return pmasternodesview->activeNodes.size();
}

bool hasAnyUnfinishedTransaction(int roundNumber)
{
//    const std::map<uint256, VoteDistribution> voteStats{calcTxVoteStats()};

//    LOCK2(cs_main, mempool.cs);

//    for (const auto& pair: voteStats) {
//        CTransaction tx{};
//        if (mempool.lookup(pair.first, tx) &&
//            tx.fInstant &&
//            pair.second.round == roundNumber &&
//            !pair.second.checkSufficiency())
//        {
//            return true;
//        }
//    }
    return false;
}


uint256 getTipBlockHash()
{
    LOCK(cs_main);
    return chainActive.Tip()->GetBlockHash();
}

template<typename T>
void relayObject(const T& obj, int type)
{
    // Expire old relay messages
    LOCK(cs_mapRelay);
    while (!vRelayExpiration.empty() &&
           vRelayExpiration.front().first < GetTime())
    {
        mapRelay.erase(vRelayExpiration.front().second);
        vRelayExpiration.pop_front();
    }

    // Save original serialized message so newer versions are preserved
    CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};
    const CInv inv{type, obj.GetHash()};

    ss.reserve(1024);
    ss << obj;

    mapRelay.insert(std::make_pair(inv, ss));
    vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
    BroadcastInventory(inv);
}

void ChainListener::UpdatedBlockTip(const CBlockIndex* pindex)
{
    libsnark::UNUSED(pindex);
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    transactionVotes.clear();
    progenitorVotes.clear();
    progenitorBlocks.clear();
}

void ChainListener::SyncTransaction(const CTransaction& tx, const CBlock* pblock)
{
    libsnark::UNUSED(pblock);
    const uint256 txHash{tx.GetHash()};
    if (mempool.exists(txHash) && tx.fInstant) {
        CTransactionVoteTracker::getInstance().voteForTransaction(tx, mns::getOperatorKey());
    }
}

bool VoteDistribution::checkSufficiency() const
{
    return 1.0 * pro / getActiveMasternodeCount() >= 2.0 / 3.0;
}

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief CVoteSignature::CVoteSignature

CVoteSignature::CVoteSignature()
{
    resize(CPubKey::COMPACT_SIGNATURE_SIZE);
}

CVoteSignature::CVoteSignature(const std::vector<unsigned char>& vch) :
    CVoteSignature{}
{
    assert(vch.size() == size());
    assign(vch.begin(), vch.end());
}

std::string CVoteSignature::ToHex() const
{
    std::ostringstream s{};
    for (std::size_t i{0}; i < size(); i++) {
        s << std::hex << static_cast<unsigned int>(at(i));
        if (i + 1 < size()) {
            s << ':';
        }
    }
    return s.str();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief CTransactionVote::CTransactionVote

CTransactionVote::CTransactionVote()
{
    SetNull();
}

bool CTransactionVote::IsNull() const
{
    return round == 0;
}

void CTransactionVote::SetNull()
{
    tip.SetNull();
    round = 0;
    choices.clear();
    signature.clear();
}

uint256 CTransactionVote::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CTransactionVote::GetSignatureHash() const
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    ss << tip
       << round
       << choices
       << salt_;
    return Hash(ss.begin(), ss.end());
}

bool CTransactionVote::containsTransaction(const CTransaction& transaction) const
{
    return std::find_if(choices.begin(), choices.end(), [&](const CVoteChoice& vote) {
        return vote.hash == transaction.GetHash();
    }) != choices.end();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief CProgenitorVote::CProgenitorVote

CProgenitorVote::CProgenitorVote()
{
    SetNull();
}

bool CProgenitorVote::IsNull() const
{
    return round == 0;
}

void CProgenitorVote::SetNull()
{
    tip.SetNull();
    round = 0;
    choice.hash.SetNull();
    signature.clear();
}

uint256 CProgenitorVote::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CProgenitorVote::GetSignatureHash() const
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    ss << tip
       << round
       << choice
       << salt_;
    return Hash(ss.begin(), ss.end());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief CTransactionVoteTracker::CTransactionVoteTracker

CTransactionVoteTracker::CTransactionVoteTracker()
{
}

CTransactionVoteTracker& CTransactionVoteTracker::getInstance()
{
    if (transactionVoteTracker_ == nullptr) {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);
        if (transactionVoteTracker_ == nullptr) {
            transactionVoteTracker_ = new CTransactionVoteTracker{};
            transactionVoteTracker_->recievedVotes = &chainListener_.transactionVotes;
        }
    }
    assert(transactionVoteTracker_ != nullptr);
    return *transactionVoteTracker_;
}

void CTransactionVoteTracker::voteForTransaction(const CTransaction& transaction, const CKey& masternodeKey)
{
    if (masternodeKey.IsValid() && !wasVotedByMe(masternodeKey, transaction)) {
        CTransactionVote vote{};
        int decision{CVoteChoice::decisionYes};

        if (interfereWithList(transaction, listMyTransactions(masternodeKey)) ||
            exeedSizeLimit(transaction) ||
            interfereWithList(transaction, dpos::listCommitedTransactions()))
        {
            decision = CVoteChoice::decisionNo;
        }
        else if (CProgenitorBlockTracker::getInstance().hasAnyReceivedBlock() ||
                 CProgenitorVoteTracker::getInstance().hasAnyReceivedVote(CVoteChoice::decisionYes))
        {
            decision = CVoteChoice::decisionPass;
        }

        vote.tip = getTipBlockHash();
        vote.round = CProgenitorBlockTracker::getInstance().getCurrentRoundNumber();
        vote.choices.push_back({transaction.GetHash(), static_cast<int8_t>(decision)});

        if (masternodeKey.SignCompact(vote.GetSignatureHash(), vote.signature)) {
            postVote(vote);
        } else {
            LogPrintf("%s: Can't vote for transaction %s", __func__, transaction.GetHash().GetHex());
        }
    }
}

void CTransactionVoteTracker::postVote(const CTransactionVote& vote)
{
    if (recieveVote(vote, true)) {
        LogPrintf("%s: Post my vote %s for transaction %s on round %d\n",
                  __func__,
                  vote.GetHash().GetHex(),
                  vote.tip.GetHex(),
                  vote.round);
        BroadcastInventory(CInv{MSG_PROGENITOR_VOTE, vote.GetHash()});
    }
}

void CTransactionVoteTracker::relayVote(const CTransactionVote& vote)
{
    if (recieveVote(vote, false)) {
        // Expire old relay messages
        LOCK(cs_mapRelay);
        while (!vRelayExpiration.empty() &&
               vRelayExpiration.front().first < GetTime())
        {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        // Save original serialized message so newer versions are preserved
        CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};
        const CInv inv{MSG_TRANSACTION_VOTE, vote.GetHash()};

        ss.reserve(1000);
        ss << vote;

        mapRelay.insert(std::make_pair(inv, ss));
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
        BroadcastInventory(inv);
    }
}

bool CTransactionVoteTracker::recieveVote(const CTransactionVote& vote, bool internal)
{
    libsnark::UNUSED(internal);

    if (checkVoteIsConvenient(vote)) {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);

        LogPrintf("%s: Transaction vote recieved: %d\n", __func__, this->recievedVotes->count(vote.GetHash()));
        if (!this->recievedVotes->emplace(vote.GetHash(), vote).second) {
            LogPrintf("%s: Ignoring duplicating transaction vote: %s\n", __func__, vote.GetHash().GetHex());
            return false;
        }
    }
    if (vote.round == CProgenitorBlockTracker::getInstance().getCurrentRoundNumber()) {
        const CKey masternodeKey{mns::getOperatorKey()};
        if (!CProgenitorVoteTracker::getInstance().wasVotedByMe(masternodeKey)) {
            for (const auto& block : CProgenitorBlockTracker::getInstance().listReceivedBlocks()) {
                if (block.nRoundNumber == vote.round) {
                    CProgenitorBlockTracker::getInstance().voteForBlock(block, masternodeKey);
                    break;
                }
            }
        }
    }

    return true;
}

bool CTransactionVoteTracker::findReceivedVote(const uint256& hash, CTransactionVote* vote)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    const auto it{this->recievedVotes->find(hash)};
    const auto rv{it != this->recievedVotes->end()};

    if (rv && vote != nullptr) {
        *vote = it->second;
    }

    return rv;
}

std::vector<CTransactionVote> CTransactionVoteTracker::listReceivedVotes()
{
    std::vector<CTransactionVote> rv{};
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);

    rv.reserve(this->recievedVotes->size());

    for (const auto& pair : *this->recievedVotes) {
        assert(pair.first == pair.second.GetHash());
        rv.emplace_back(pair.second);
    }

    return rv;
}

std::vector<CTransaction> CTransactionVoteTracker::listMyTransactions(const CKey& masternodeKey)
{
    std::vector<CTransaction> rv{};

    LOCK2(cs_main, mempool.cs);
    for (const auto& vote: listReceivedVotes()) {
        CPubKey pubKey{};
        if (pubKey.RecoverCompact(vote.GetSignatureHash(), vote.signature) &&
            pubKey == masternodeKey.GetPubKey())
        {
            for (const auto& choice: vote.choices) {
                rv.resize(rv.size() + 1);
                if (!mempool.lookup(choice.hash, rv.back())) {
                    rv.pop_back();
                }
            }
        }
    }

    return std::move(rv);
}

bool CTransactionVoteTracker::wasVotedByMe(const CKey& masternodeKey, const CTransaction& transaction)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);

    for (const auto& pair: *this->recievedVotes) {
        CPubKey pubKey{};
        if (pubKey.RecoverCompact(pair.second.GetSignatureHash(), pair.second.signature) &&
            pubKey == masternodeKey.GetPubKey() &&
            pair.second.containsTransaction(transaction))
        {
            return true;
        }
    }

    return false;
}


bool CTransactionVoteTracker::checkVoteIsConvenient(const CTransactionVote& vote)
{
    return vote.tip == getTipBlockHash();
}

bool CTransactionVoteTracker::interfereWithList(const CTransaction& transaction, const std::vector<CTransaction>& txList)
{
    return false;
}


bool CTransactionVoteTracker::exeedSizeLimit(const CTransaction& transaction)
{
    std::size_t size{GetSerializeSize(transaction, SER_NETWORK, PROTOCOL_VERSION)};

    for (const auto& tx : dpos::listCommitedTransactions()) {
        size += GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    }

    return size >= DPOS_SECTION_SIZE;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief CProgenitorVoteTracker::CProgenitorVoteTracker

CProgenitorVoteTracker::CProgenitorVoteTracker()
{
}

CProgenitorVoteTracker& CProgenitorVoteTracker::getInstance()
{
    if (progenitorVoteTrackerInstance_ == nullptr) {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);
        if (progenitorVoteTrackerInstance_ == nullptr) {
            progenitorVoteTrackerInstance_ = new CProgenitorVoteTracker{};
            progenitorVoteTrackerInstance_->recievedVotes = &chainListener_.progenitorVotes;
        }
    }
    assert(progenitorVoteTrackerInstance_ != nullptr);
    return *progenitorVoteTrackerInstance_;
}

void CProgenitorVoteTracker::postVote(const CProgenitorVote& vote)
{
    if (recieveVote(vote, true)) {
        LogPrintf("%s: Post my vote %s for pre-block %s on round %d\n",
                  __func__,
                  vote.GetHash().GetHex(),
                  vote.tip.GetHex(),
                  vote.round);
        BroadcastInventory(CInv{MSG_PROGENITOR_VOTE, vote.GetHash()});
    }
}

void CProgenitorVoteTracker::relayVote(const CProgenitorVote& vote)
{
    if (recieveVote(vote, false)) {
//        // Expire old relay messages
//        LOCK(cs_mapRelay);
//        while (!vRelayExpiration.empty() &&
//               vRelayExpiration.front().first < GetTime())
//        {
//            mapRelay.erase(vRelayExpiration.front().second);
//            vRelayExpiration.pop_front();
//        }

//        // Save original serialized message so newer versions are preserved
//        CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};
//        const CInv inv{MSG_PROGENITOR_VOTE, vote.GetHash()};

//        ss.reserve(1000);
//        ss << vote;

//        mapRelay.insert(std::make_pair(inv, ss));
//        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
//        BroadcastInventory(inv);
        relayObject(vote, MSG_PROGENITOR_VOTE);
    }
}

bool CProgenitorVoteTracker::recieveVote(const CProgenitorVote& vote, bool internal)
{
    using PVPair = std::pair<uint256, std::size_t>;
    std::map<PVPair::first_type, PVPair::second_type> progenitorVotes{};

    if (checkVoteIsConvenient(vote)) {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);

        LogPrintf("%s: Pre-block vote recieved: %d\n", __func__, this->recievedVotes->count(vote.GetHash()));
        if (this->recievedVotes->emplace(vote.GetHash(), vote).second) {
            for (const auto& pair : *this->recievedVotes) {
                if (pair.second.choice.decision == CVoteChoice::decisionYes) {
                    progenitorVotes[pair.second.choice.hash]++;
                }
            }
        }
    }

    const auto itEnd{progenitorVotes.end()};
    const auto itMax{std::max_element(progenitorVotes.begin(), itEnd, [](const PVPair& p1, const PVPair& p2) {
        return p1.second < p2.second;
    })};
    if (itMax == itEnd) {
        LogPrintf("%s: Ignoring duplicating pre-block vote: %s\n", __func__, vote.GetHash().GetHex());
        return false;
    }

    const CKey operKey{mns::getOperatorKey()};
    if (operKey.IsValid()) {
        LogPrintf("%s: Pre-block vote rate: %d\n", __func__, 1.0 * itMax->second / getActiveMasternodeCount());
        if (internal && 1.0 * itMax->second / getActiveMasternodeCount() >= 2.0 / 3.0) {
            CBlock pblock{};
            if (findProgenitorBlock(itMax->first, &pblock)) {
                CValidationState state{};
                CBlock dposBlock{pblock};

                if (dposBlock.GetHash() != itMax->first ||
                    !ProcessNewBlock(state, NULL, &dposBlock, true, NULL))
                {
                    LogPrintf("%s: Can't create new dpos block\n");
                }
            }
        }
//        else if (recievedProgenitorVotes_.size() == pmasternodesview->activeNodes.size()) {
//            int roundNumber{recievedProgenitorVotes_.begin()->second.roundNumber + 1};
//            recievedProgenitorVotes_.clear();
//            assert(!recievedProgenitorBlocks_.empty());
//            voteForProgenitorBlock(recievedProgenitorBlocks_.begin()->second, operKey, roundNumber);
//        }
    }
    return true;
}

bool CProgenitorVoteTracker::findReceivedVote(const uint256& hash, CProgenitorVote* vote)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    const auto it{this->recievedVotes->find(hash)};
    const auto rv{it != this->recievedVotes->end()};

    if (rv && vote != nullptr) {
        *vote = it->second;
    }

    return rv;
}

bool CProgenitorVoteTracker::hasAnyReceivedVote(int roundNumber, int decision) const
{
    for (const auto& vote : listReceivedVotes()) {
        if (vote.round == roundNumber &&
            (vote.choice.decision < 0 || vote.choice.decision == decision))
        {
            return true;
        }
    }
    return false;
}

bool CProgenitorVoteTracker::wasVotedByMe(const CKey& masternodeKey)
{
    for (const auto& vote: listReceivedVotes()) {
        CBlock block{};
        CPubKey pubKey{};
        if (pubKey.RecoverCompact(vote.GetSignatureHash(), vote.signature) &&
            pubKey == masternodeKey.GetPubKey() &&
            CProgenitorBlockTracker::getInstance().findReceivedBlock(vote.choice.hash, &block) &&
            block.nRoundNumber == CProgenitorBlockTracker::getInstance().getCurrentRoundNumber())
        {
            return true;
        }

    }
    return false;
}

std::vector<CProgenitorVote> CProgenitorVoteTracker::listReceivedVotes() const
{
    std::vector<CProgenitorVote> rv{};
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);

    rv.reserve(this->recievedVotes->size());

    for (const auto& pair : *this->recievedVotes) {
        assert(pair.first == pair.second.GetHash());
        rv.emplace_back(pair.second);
    }

    return rv;
}

bool CProgenitorVoteTracker::checkVoteIsConvenient(const CProgenitorVote& vote)
{
    return vote.tip == getTipBlockHash() &&
           CProgenitorBlockTracker::getInstance().findReceivedBlock(vote.choice.hash);
}

bool CProgenitorVoteTracker::findProgenitorBlock(const uint256& dposBlockHash, CBlock* block)
{
    for (const auto& pair: *this->recievedVotes) {
        if (pair.second.choice.hash == dposBlockHash) {
            return CProgenitorBlockTracker::getInstance().findReceivedBlock(dposBlockHash, block);
        }
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief CProgenitorBlockTracker::CProgenitorBlockTracker

CProgenitorBlockTracker::CProgenitorBlockTracker()
{
}

CProgenitorBlockTracker& CProgenitorBlockTracker::getInstance()
{
    if (progenitorBlockTrackerInstance_ == nullptr) {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);
        if (progenitorBlockTrackerInstance_ == nullptr) {
            progenitorBlockTrackerInstance_ = new CProgenitorBlockTracker{};
            progenitorBlockTrackerInstance_->recievedBlocks = &chainListener_.progenitorBlocks;
        }
    }
    assert(progenitorBlockTrackerInstance_ != nullptr);
    return *progenitorBlockTrackerInstance_;
}

void CProgenitorBlockTracker::postBlock(const CBlock& block)
{
    if (recieveBlock(block, true)) {
        BroadcastInventory({MSG_PROGENITOR_BLOCK, block.GetHash()});
    }
}

void CProgenitorBlockTracker::relayBlock(const CBlock& block)
{
    if (recieveBlock(block, false)) {
        relayObject(block, MSG_PROGENITOR_BLOCK);
    }
}

bool CProgenitorBlockTracker::voteForBlock(const CBlock& progenitorBlock, const CKey& masternodeKey)
{
    const int currentRound{getCurrentRoundNumber()};

    if (masternodeKey.IsValid() &&
        !hasAnyUnfinishedTransaction(currentRound) &&
        CProgenitorVoteTracker::getInstance().hasAnyReceivedVote(getCurrentRoundNumber()))
    {
        return true;
    }

//    if (masternodeKey.IsValid() &&
//        !hasAnyUnfinishedTransaction() &&
//        !CProgenitorVoteTracker::getInstance().wasVotedByMe(masternodeKey))
//    {
//        CProgenitorVote vote{};
//        vote.choice.hash = progenitorBlock.GetHash();
////        vote.roundNumber = progenitorBlock.nRoundNumber;
//        vote.tip = progenitorBlock.hashPrevBlock;

//        if (!masternodeKey.SignCompact(vote.GetSignatureHash(), vote.signature)) {
//            LogPrintf("%s: Can't vote for pre-block %s", __func__, progenitorBlock.GetHash().GetHex());
//        } else {
//            CProgenitorVoteTracker::getInstance().postVote(vote);
//            return true;
//        }
//    }
    return false;
}

bool CProgenitorBlockTracker::recieveBlock(const CBlock& block, bool isMe)
{
    bool rv{false};
    libsnark::UNUSED(isMe);

    if (checkBlockIsConvenient(block)) {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);
        rv = this->recievedBlocks->emplace(block.GetHash(), block).second;
    }

    if (rv) {
        voteForBlock(block, mns::getOperatorKey());
    } else {
        LogPrintf("%s: Ignoring duplicating pre-block: %s\n", __func__, block.GetHash().GetHex());
    }

    return rv;
}

bool CProgenitorBlockTracker::findReceivedBlock(const uint256& hash, CBlock* block) const
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    const auto it{this->recievedBlocks->find(hash)};
    const auto rv{it != this->recievedBlocks->end()};

    if (rv && block != nullptr) {
        *block = it->second;
    }

    return rv;
}

bool CProgenitorBlockTracker::hasAnyReceivedBlock() const
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    return !this->recievedBlocks->empty();
}

std::vector<CBlock> CProgenitorBlockTracker::listReceivedBlocks() const
{
    std::vector<CBlock> rv{};
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);

    rv.reserve(this->recievedBlocks->size());

    for (const auto& pair : *this->recievedBlocks) {
        assert(pair.first == pair.second.GetHash());
        rv.emplace_back(pair.second);
    }

    return rv;
}

int CProgenitorBlockTracker::getCurrentRoundNumber() const
{
    const std::vector<CBlock> blocks{listReceivedBlocks()};
    const auto it{std::min_element(blocks.begin(), blocks.end(), [](const CBlock& lhs, const CBlock& rhs) {
        return lhs.nRoundNumber < rhs.nRoundNumber;
    })};
    return it != blocks.end() ? it->nRoundNumber : 1;
}

//void CProgenitorBlockTracker::doRoundVoting(const CKey& masternodeKey)
//{
//    const int currentRound{getCurrentRoundNumber()};

//    if (masternodeKey.IsValid() &&
//        !hasAnyUnfinishedTransaction(currentRound) &&
//        CProgenitorVoteTracker::hasAnyReceivedVote(getCurrentRoundNumber())) {

//    }
//}

bool CProgenitorBlockTracker::checkBlockIsConvenient(const CBlock& block)
{
    return block.hashPrevBlock == getTipBlockHash();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief dpos::checkIsActive

bool dpos::isActive()
{
    const CChainParams& params{Params()};
    LOCK(cs_main);
    return NetworkUpgradeActive(chainActive.Height(), params.GetConsensus(), Consensus::UPGRADE_SAPLING) &&
           getActiveMasternodeCount() >= params.GetMinimalMasternodeCount();
}

CValidationInterface* dpos::getValidationListener()
{
    return &chainListener_;
}

std::vector<CTransaction> dpos::listCommitedTransactions()
{
    std::vector<CTransaction> rv{};
    const std::map<uint256, VoteDistribution> voteStats{calcTxVoteStats()};

    rv.reserve(voteStats.size());
    LOCK2(cs_main, mempool.cs);

    for (const auto& pair: voteStats) {
        rv.resize(rv.size() + 1);
        if (!mempool.lookup(pair.first, rv.back()) ||
            !rv.back().fInstant ||
            !pair.second.checkSufficiency())
        {
            rv.pop_back();
        }
    }

    return rv;
}

