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
std::array<unsigned char, 16> salt{0x4D, 0x48, 0x7A, 0x52, 0x5D, 0x4D, 0x37, 0x78, 0x42, 0x36, 0x5B, 0x64, 0x44, 0x79, 0x59, 0x4F};

class ValidationListener : public CValidationInterface
{
public:
    std::map<uint256, CTransactionVote> transactionVotes;
    std::map<uint256, CProgenitorVote> progenitorVotes;
    std::map<uint256, CBlock> progenitorBlocks;

protected:
    void UpdatedBlockTip(const CBlockIndex* pindex) override
    {
        libsnark::UNUSED(pindex);
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);
        transactionVotes.clear();
        progenitorVotes.clear();
        progenitorBlocks.clear();
    }

    void SyncTransaction(const CTransaction& tx, const CBlock* pblock) override
    {
        libsnark::UNUSED(pblock);
        const uint256 txHash{tx.GetHash()};
        if (mempool.exists(txHash)) {
            CTransactionVoteTracker::getInstance().vote(tx, mns::extractOperatorKey());
        }
    }
} validationListener_;

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


CBlock tranformProgenitorBlock(const CBlock& progenitorBlock)
{
    CBlock rv{progenitorBlock.GetBlockHeader()};
    rv.nRoundNumber = progenitorBlock.nRoundNumber;
    rv.vtx.resize(progenitorBlock.vtx.size());
    std::copy(progenitorBlock.vtx.begin(), progenitorBlock.vtx.end(), rv.vtx.begin());
    rv.hashMerkleRoot = rv.BuildMerkleTree();
    //        attachTransactions(dposBlock);
    return rv;
}


void printBlock(const CBlock& block)
{
    const auto toHex{[](const std::vector<unsigned char>& bin) {
         std::ostringstream ss{};
         for (const auto& v: bin) {
            ss << std::hex << static_cast<int>(v) << ':';
         }
         return ss.str();
    }};
    LogPrintf("%s: hash: %s, hashPrev: %s, merkleRoot: %s, round: %d, bits: %d, time: %d, solution: %s\n",
              __func__,
              block.GetHash().GetHex(),
              block.hashPrevBlock.GetHex(),
              block.hashMerkleRoot.GetHex(),
              block.nRoundNumber,
              block.nBits,
              block.nTime,
              toHex(block.nSolution));
}

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief CVoteSignature::CVoteSignature

CVoteSignature::CVoteSignature()
{
    resize(CPubKey::COMPACT_SIGNATURE_SIZE);
}

CVoteSignature::CVoteSignature(const std::vector<unsigned char>& vch) : CVoteSignature{}
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
    return roundNumber == 0;
}

void CTransactionVote::SetNull()
{
    tipBlockHash.SetNull();
    roundNumber = 0;
    choices.clear();
    authSignature.clear();
}

uint256 CTransactionVote::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CTransactionVote::GetSignatureHash() const
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    ss << tipBlockHash
       << roundNumber
       << choices
       << salt;
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
    return roundNumber == 0;
}

void CProgenitorVote::SetNull()
{
    tipBlockHash.SetNull();
    roundNumber = 0;
    choice.hash.SetNull();
    authSignature.clear();
}

uint256 CProgenitorVote::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CProgenitorVote::GetSignatureHash() const
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    ss << tipBlockHash
       << roundNumber
       << choice
       << salt;
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
            transactionVoteTracker_->recievedVotes = &validationListener_.transactionVotes;
        }
    }
    assert(transactionVoteTracker_ != nullptr);
    return *transactionVoteTracker_;
}

void CTransactionVoteTracker::vote(const CTransaction& transaction, const CKey& operatorKey)
{
    if (operatorKey.IsValid() && findMyVote(operatorKey, transaction) == nullptr) {
        CTransactionVote vote{};
        LOCK(cs_main);
        vote.tipBlockHash = chainActive.Tip()->GetBlockHash();
        vote.roundNumber = 1;
        vote.choices.push_back({transaction.GetHash(), CVoteChoice::decisionYes});

        if (operatorKey.SignCompact(vote.GetSignatureHash(), vote.authSignature)) {
            post(vote);
        } else {
            LogPrintf("%s: Can't vote for transaction %s", __func__, transaction.GetHash().GetHex());
        }
    }
}

void CTransactionVoteTracker::post(const CTransactionVote& vote)
{
    if (recieve(vote, true)) {
        LogPrintf("%s: Post my vote %s for transaction %s on round %d\n",
                  __func__,
                  vote.GetHash().GetHex(),
                  vote.tipBlockHash.GetHex(),
                  vote.roundNumber);
        BroadcastInventory(CInv{MSG_PROGENITOR_VOTE, vote.GetHash()});
    }
}

void CTransactionVoteTracker::relay(const CTransactionVote& vote)
{
    if (recieve(vote, false)) {
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

bool CTransactionVoteTracker::recieve(const CTransactionVote& vote, bool isMe)
{
    using PVPair = std::pair<uint256, std::size_t>;
    std::map<PVPair::first_type, PVPair::second_type> progenitorVotes{};

    if (checkVoteIsConvenient(vote)) {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);

        LogPrintf("%s: Transaction vote recieved: %d\n", __func__, this->recievedVotes->count(vote.GetHash()));
        if (!this->recievedVotes->emplace(vote.GetHash(), vote).second) {
            LogPrintf("%s: Ignoring duplicating transaction vote: %s\n", __func__, vote.GetHash().GetHex());
        }
    }

    return true;
}

const CTransactionVote* CTransactionVoteTracker::getReceivedVote(const uint256& hash)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    const auto it{this->recievedVotes->find(hash)};
    return it == this->recievedVotes->end() ? nullptr : &it->second;
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

const CTransactionVote* CTransactionVoteTracker::findMyVote(const CKey& key, const CTransaction& transaction)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);

    for (const auto& pair: *this->recievedVotes) {
        CPubKey pubKey{};
        if (pubKey.RecoverCompact(pair.second.GetSignatureHash(), pair.second.authSignature)) {
            if (pubKey == key.GetPubKey()) {
                if (pair.second.containsTransaction(transaction)) {
                    return &pair.second;
                }
            }
        }

    }
    return nullptr;
}

bool CTransactionVoteTracker::checkVoteIsConvenient(const CTransactionVote& vote)
{
    LOCK(cs_main);
    return vote.tipBlockHash == chainActive.Tip()->GetBlockHash();
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
            progenitorVoteTrackerInstance_->recievedVotes = &validationListener_.progenitorVotes;
        }
    }
    assert(progenitorVoteTrackerInstance_ != nullptr);
    return *progenitorVoteTrackerInstance_;
}

void CProgenitorVoteTracker::post(const CProgenitorVote& vote)
{
    if (recieve(vote, true)) {
        LogPrintf("%s: Post my vote %s for pre-block %s on round %d\n",
                  __func__,
                  vote.GetHash().GetHex(),
                  vote.tipBlockHash.GetHex(),
                  vote.roundNumber);
        BroadcastInventory(CInv{MSG_PROGENITOR_VOTE, vote.GetHash()});
    }
}

void CProgenitorVoteTracker::relay(const CProgenitorVote& vote)
{
    if (recieve(vote, false)) {
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
        const CInv inv{MSG_PROGENITOR_VOTE, vote.GetHash()};

        ss.reserve(1000);
        ss << vote;

        mapRelay.insert(std::make_pair(inv, ss));
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
        BroadcastInventory(inv);
    }
}

bool CProgenitorVoteTracker::recieve(const CProgenitorVote& vote, bool isMe)
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

    const CKey operKey{mns::extractOperatorKey()};
    if (operKey.IsValid()) {
        LogPrintf("%s: Pre-block vote rate: %d\n", __func__, 1.0 * itMax->second / pmasternodesview->activeNodes.size());
        if (isMe && 1.0 * itMax->second / pmasternodesview->activeNodes.size() >= 2.0 / 3.0) {
            const CBlock* pBlock{findProgenitorBlock(itMax->first)};
            if (pBlock != nullptr) {
                CValidationState state{};
                CBlock dposBlock{tranformProgenitorBlock(*pBlock)};

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

const CProgenitorVote* CProgenitorVoteTracker::findMyVote(const CKey& key)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);

    for (const auto& pair: *this->recievedVotes) {
        CPubKey pubKey{};
        if (pubKey.RecoverCompact(pair.second.GetSignatureHash(), pair.second.authSignature)) {
            if (pubKey == key.GetPubKey()) {
                return &pair.second;
            }
        }

    }
    return nullptr;
}

const CProgenitorVote* CProgenitorVoteTracker::getReceivedVote(const uint256& hash)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    const auto it{this->recievedVotes->find(hash)};
    return it == this->recievedVotes->end() ? nullptr : &it->second;
}


std::vector<CProgenitorVote> CProgenitorVoteTracker::listReceivedVotes()
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

const CBlock* CProgenitorVoteTracker::findProgenitorBlock(const uint256& dposBlockHash)
{
    for (const auto& pair: *this->recievedVotes) {
        if (pair.second.choice.hash == dposBlockHash) {
            return CProgenitorBlockTracker::getInstance().getReceivedBlock(dposBlockHash);
        }
    }
    return nullptr;
}

bool CProgenitorVoteTracker::checkVoteIsConvenient(const CProgenitorVote& vote)
{
    LOCK(cs_main);
    return vote.tipBlockHash == chainActive.Tip()->GetBlockHash() &&
           CProgenitorBlockTracker::getInstance().getReceivedBlock(vote.choice.hash) != nullptr;
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
            progenitorBlockTrackerInstance_->recievedBlocks = &validationListener_.progenitorBlocks;
        }
    }
    assert(progenitorBlockTrackerInstance_ != nullptr);
    return *progenitorBlockTrackerInstance_;
}

bool CProgenitorBlockTracker::vote(const CBlock& progenitorBlock, const CKey& operatorKey)
{
    if (operatorKey.IsValid() && CProgenitorVoteTracker::getInstance().findMyVote(operatorKey) == nullptr) {
        CProgenitorVote vote{};
        vote.choice.hash = progenitorBlock.GetHash();
        vote.roundNumber = progenitorBlock.nRoundNumber;
        vote.tipBlockHash = progenitorBlock.hashPrevBlock;
        vote.authSignature.resize(CPubKey::COMPACT_SIGNATURE_SIZE);

        if (operatorKey.SignCompact(vote.GetSignatureHash(), vote.authSignature)) {
            CProgenitorVoteTracker::getInstance().post(vote);
        } else {
            LogPrintf("%s: Can't vote for pre-block %s", __func__, progenitorBlock.GetHash().GetHex());
        }
    }
}

void CProgenitorBlockTracker::post(const CBlock& block)
{
    if (recieve(block, true)) {
        BroadcastInventory({MSG_PROGENITOR_BLOCK, block.GetHash()});
    }
}

void CProgenitorBlockTracker::relay(const CBlock& block)
{
    if (recieve(block, false)) {
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
        const CInv inv{MSG_PROGENITOR_BLOCK, block.GetHash()};

        ss.reserve(1000);
        ss << block;

        mapRelay.insert(std::make_pair(inv, ss));
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
        BroadcastInventory(inv);
    }
}

bool CProgenitorBlockTracker::recieve(const CBlock& block, bool isMe)
{
    bool rv{false};
    libsnark::UNUSED(isMe);

    if (checkBlockIsConvenient(block)) {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);
        rv = this->recievedBlocks->emplace(block.GetHash(), block).second;
    }

    if (rv) {
        vote(block, mns::extractOperatorKey());
    } else {
        LogPrintf("%s: Ignoring duplicating pre-block: %s\n", __func__, block.GetHash().GetHex());
    }

    return rv;
}

const CBlock* CProgenitorBlockTracker::getReceivedBlock(const uint256& hash)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);
    const auto it{this->recievedBlocks->find(hash)};
    return it == this->recievedBlocks->end() ? nullptr : &it->second;
}

std::vector<CBlock> CProgenitorBlockTracker::listReceivedBlocks()
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
    LOCK(cs_main);
    return block.hashPrevBlock == chainActive.Tip()->GetBlockHash();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief dpos::checkIsActive

bool dpos::checkIsActive()
{
    const CChainParams& params{Params()};
    LOCK(cs_main);
    return NetworkUpgradeActive(chainActive.Height(), params.GetConsensus(), Consensus::UPGRADE_SAPLING) &&
           pmasternodesview->activeNodes.size() >= params.GetMinimalMasternodeCount();
}

CValidationInterface* dpos::getValidationListener()
{
    return &validationListener_;
}
