// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "dpos.h"
#include "../protocol.h"
#include "../net.h"
#include "../main.h"
#include "../init.h"
#include "../key.h"
#include "../chainparams.h"
#include "../validationinterface.h"
#include "../wallet/wallet.h"
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

uint256 getTipBlockHash()
{
    LOCK(cs_main);
    return chainActive.Tip()->GetBlockHash();
}

CKey getMasternodeKey()
{
    CKey rv{};
#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain->cs_wallet);
    const boost::optional<CMasternodesView::CMasternodeIDs> mnId{pmasternodesview->AmIActiveOperator()};
    if (mnId != boost::none) {
        if (!pwalletMain->GetKey(mnId.get().operatorAuthAddress, rv)) {
            rv = CKey{};
        }
    }
#endif
    return rv;
}

std::size_t getActiveMasternodeCount()
{
    LOCK(cs_main);
    return pmasternodesview->GetActiveMasternodes().size();
}

std::map<uint256, VoteDistribution> calcTransactionVoteStats()
{
    std::map<uint256, VoteDistribution> rv{};

    for (const auto& vote : CTransactionVoteTracker::getInstance().listReceivedVotes()) {
        for (const auto& choice : vote.choices) {
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

std::map<uint256, VoteDistribution> calcProgenitorVoteStats()
{
    std::map<uint256, VoteDistribution> rv{};

    for (const auto& vote : CProgenitorVoteTracker::getInstance().listReceivedVotes()) {
        VoteDistribution& stats{rv[vote.choice.hash]};
        switch (vote.choice.decision) {
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

    return rv;
}

bool interfereWithList(const CTransaction& transaction, const std::vector<CTransaction>& txList)
{
    libsnark::UNUSED(transaction);
    libsnark::UNUSED(txList);
    return false;
}

bool interfereWithList(const CBlock& block, const std::vector<CTransaction>& txList)
{
    libsnark::UNUSED(block);
    libsnark::UNUSED(txList);
    return false;
}

bool hasAnyUnfinishedTransaction(int roundNumber)
{
    libsnark::UNUSED(roundNumber);
    return false;
}

int getCurrentRoundNumber()
{
    const std::vector<CBlock> blocks{CProgenitorBlockTracker::getInstance().listReceivedBlocks()};
    const auto it{std::min_element(blocks.begin(), blocks.end(), [](const CBlock& lhs, const CBlock& rhs) {
        return lhs.nRoundNumber < rhs.nRoundNumber;
    })};
    return it != blocks.end() ? it->nRoundNumber : 1;
}

void voteForBestProgenitorBlock()
{
    const CKey masternodeKey{getMasternodeKey()};
    CProgenitorVoteTracker& voteTracker{CProgenitorVoteTracker::getInstance()};
    CProgenitorBlockTracker& blockTracker{CProgenitorBlockTracker::getInstance()};

    if (masternodeKey.IsValid() &&
        !hasAnyUnfinishedTransaction(getCurrentRoundNumber()) &&
        !voteTracker.wasVotedByMe(masternodeKey))
    {
        // sort the pre-blocks by number of votes, pre-block Hash (decreasing)
        std::map<std::size_t, std::set<arith_uint256>> bestMap{};
        for (const auto& pair: calcProgenitorVoteStats()) {
            bestMap[pair.second.pro].insert(UintToArith256(pair.first));
        }
        for (auto it{bestMap.rbegin()}, end{bestMap.rend()}; it != end; ++it) {
            for (const auto& arithBlockHash : it->second) {
                CBlock block{};
                if (blockTracker.findReceivedBlock(ArithToUint256(arithBlockHash), &block) &&
                    voteTracker.voteForBlock(block, masternodeKey))
                {
                    return;
                }
            }
        }
    }
    LogPrintf("%s: Can't find best progenitor block", __func__);
}

template<typename T>
void relayEntity(const T& obj, int type)
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
        CTransactionVoteTracker::getInstance().voteForTransaction(tx, getMasternodeKey());
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

bool CTransactionVoteTracker::voteForTransaction(const CTransaction& transaction, const CKey& masternodeKey)
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
        vote.round = getCurrentRoundNumber();
        vote.choices.push_back({transaction.GetHash(), static_cast<int8_t>(decision)});

        if (!masternodeKey.SignCompact(vote.GetSignatureHash(), vote.signature)) {
            LogPrintf("%s: Can't vote for transaction %s", __func__, transaction.GetHash().GetHex());
        } else {
            postVote(vote);
            return true;
        }
    }
    return false;
}

void CTransactionVoteTracker::postVote(const CTransactionVote& vote)
{
    if (recieveVote(vote, true)) {
        LogPrintf("%s: Post my vote %s for transaction %s on round %d\n",
                  __func__,
                  vote.GetHash().GetHex(),
                  vote.choices.front().hash.GetHex(),
                  vote.round);
        BroadcastInventory(CInv{MSG_PROGENITOR_VOTE, vote.GetHash()});
    }
}

void CTransactionVoteTracker::relayVote(const CTransactionVote& vote)
{
    if (recieveVote(vote, false)) {
        relayEntity(vote, MSG_TRANSACTION_VOTE);
    }
}

bool CTransactionVoteTracker::recieveVote(const CTransactionVote& vote, bool internal)
{
    libsnark::UNUSED(internal);

    if (!checkVoteIsConvenient(vote)) {
        return false;
    } else {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);

        if (!this->recievedVotes->emplace(vote.GetHash(), vote).second) {
            LogPrintf("%s: Ignoring duplicating transaction vote %s\n", __func__, vote.GetHash().GetHex());
            return false;
        }
    }

    LogPrintf("%s: Recieved transaction vote %s\n", __func__, vote.GetHash().GetHex());
    if (vote.round == getCurrentRoundNumber()) {
        if (!CProgenitorVoteTracker::getInstance().wasVotedByMe(getMasternodeKey())) {
            voteForBestProgenitorBlock();
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
    const std::vector<CTransactionVote> votes{listReceivedVotes()};

    LOCK2(cs_main, mempool.cs);
    for (const auto& vote: votes) {
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

    return rv;
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

bool CProgenitorVoteTracker::voteForBlock(const CBlock& progenitorBlock, const CKey& masternodeKey)
{
    //  first valid (against commited list) pre-block
    if (masternodeKey.IsValid() && !interfereWithList(progenitorBlock, dpos::listCommitedTransactions())) {
        CProgenitorVote vote{};

        vote.tip = getTipBlockHash();
        vote.round = getCurrentRoundNumber();
        vote.choice.hash = progenitorBlock.GetHash();
        vote.choice.decision = static_cast<int8_t>(CVoteChoice::decisionYes);

        if (!masternodeKey.SignCompact(vote.GetSignatureHash(), vote.signature)) {
            LogPrintf("%s: Can't vote for progenitor block %s", __func__, progenitorBlock.GetHash().GetHex());
        } else {
            postVote(vote);
            return true;
        }
    }
    return false;
}

void CProgenitorVoteTracker::postVote(const CProgenitorVote& vote)
{
    if (recieveVote(vote, true)) {
        LogPrintf("%s: Posted my vote %s for pre-block %s on round %d\n",
                  __func__,
                  vote.GetHash().GetHex(),
                  vote.choice.hash.GetHex(),
                  vote.round);
        BroadcastInventory(CInv{MSG_PROGENITOR_VOTE, vote.GetHash()});
    }
}

void CProgenitorVoteTracker::relayVote(const CProgenitorVote& vote)
{
    if (recieveVote(vote, false)) {
        relayEntity(vote, MSG_PROGENITOR_VOTE);
    }
}

bool CProgenitorVoteTracker::recieveVote(const CProgenitorVote& vote, bool internal)
{
    libsnark::UNUSED(internal);

    if (!checkVoteIsConvenient(vote)) {
        return false;
    } else {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);

        if (!this->recievedVotes->emplace(vote.GetHash(), vote).second) {
            LogPrintf("%s: Ignoring duplicating pre-block vote: %s\n", __func__, vote.GetHash().GetHex());
            return false;
        }
    }

    LogPrintf("%s: Recieved pre-block vote %s\n", __func__, vote.GetHash().GetHex());

    if (checkStalemate()) {
        // on new round
    } else {
        for (const auto& pair : calcProgenitorVoteStats()) {
            if (pair.second.checkSufficiency()) {
                CBlock dposBlock{};
                if (CProgenitorBlockTracker::getInstance().findReceivedBlock(pair.first, &dposBlock)) {
                    CValidationState state{};
                    if (ProcessNewBlock(state, NULL, &dposBlock, true, NULL)) {
                        break;
                    }
                }
                LogPrintf("%s: Can't create new dpos block\n");
                break;
            }
        }
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
            block.nRoundNumber == getCurrentRoundNumber())
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

bool CProgenitorVoteTracker::checkStalemate()
{
    return false;
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
        LogPrintf("%s: Posted pre-block %s\n",
                  __func__,
                  block.GetHash().GetHex());
        BroadcastInventory({MSG_PROGENITOR_BLOCK, block.GetHash()});
    }
}

void CProgenitorBlockTracker::relayBlock(const CBlock& block)
{
    if (recieveBlock(block, false)) {
        relayEntity(block, MSG_PROGENITOR_BLOCK);
    }
}

bool CProgenitorBlockTracker::recieveBlock(const CBlock& block, bool internal)
{
    libsnark::UNUSED(internal);

    if (!checkBlockIsConvenient(block)) {
        return false;
    } else {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);
        if (!this->recievedBlocks->emplace(block.GetHash(), block).second) {
            LogPrintf("%s: Ignoring duplicating pre-block: %s\n", __func__, block.GetHash().GetHex());
            return false;
        }
    }

    LogPrintf("%s: Recieved pre-block %s\n", __func__, block.GetHash().GetHex());
    voteForBestProgenitorBlock();
    return true;
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
    const std::map<uint256, VoteDistribution> voteStats{calcTransactionVoteStats()};

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

//TODO:
// 1. passed 20 minutes since round start (local time)
// 2. stalemate while voting for transactions and pre-blocks
// 3. save and restore votes to DB
// 4. retrieve votes from p2p messages
// 5. update rpc-methods (getinfo, getbalance, listunpent, etc...)
// 6. reindex/restart
