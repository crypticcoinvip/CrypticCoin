// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dpos.h"
#include "util.h"
#include "../init.h"
#include "../protocol.h"
#include "../net.h"
#include "../chainparams.h"
#include "../validationinterface.h"
#include "../wallet/wallet.h"
#include "../masternodes/masternodes.h"
#include "../consensus/upgrades.h"
#include "../consensus/validation.h"
#include "../snark/libsnark/common/utils.hpp"
#include <mutex>

namespace
{
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
} validationListener_;

void attachTransactions(CBlock& block)
{
    const int nHeight{chainActive.Tip()->nHeight + 1};
    const int64_t nMedianTimePast{chainActive.Tip()->GetMedianTimePast()};

    block.vtxSize_dPoS = block.vtx.size();
    for (auto mi{mempool.mapTx.begin()}; mi != mempool.mapTx.end(); ++mi) {
        const CTransaction& tx = mi->GetTx();
        const int64_t nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                                      ? nMedianTimePast
                                      : block.GetBlockTime();

        if (!tx.IsCoinBase() && IsFinalTx(tx, nHeight, nLockTimeCutoff) && !IsExpiredTx(tx, nHeight)) {
            block.vtx.push_back(tx);
        }
    }
    block.vtxSize_dPoS = block.vtx.size() - block.vtxSize_dPoS;
}

CKey extractOperatorKey()
{
    CKey rv{};
#ifdef ENABLE_WALLET
    const boost::optional<mns::CMasternodeIDs> mnId{mns::amIActiveOperator()};
    if (mnId) {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        if (!pwalletMain->GetKey(mnId.get().operatorAuthAddress, rv)) {
            rv = CKey{};
        }
    }
#endif
    return rv;
}

CBlock tranformProgenitorBlock(const CBlock& progenitorBlock)
{
    CBlock rv{progenitorBlock.GetBlockHeader()};
    rv.roundNumber = progenitorBlock.roundNumber;
    rv.vtx.resize(progenitorBlock.vtx.size());
    std::copy(progenitorBlock.vtx.begin(), progenitorBlock.vtx.end(), rv.vtx.begin());
    rv.hashMerkleRoot = rv.BuildMerkleTree();
    //        attachTransactions(dposBlock);
    return rv;
}


bool checkProgenitorBlockIsConvenient(const CBlock& block)
{
    LOCK(cs_main);
    return block.hashPrevBlock == chainActive.Tip()->GetBlockHash();
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
    LogPrintf("%s: hash: %s, hashPrev: %s, merkleRoot: %s, merkleRoot_PoW: %s, round: %d, bits: %d, time: %d, solution: %s\n",
              __func__,
              block.GetHash().GetHex(),
              block.hashPrevBlock.GetHex(),
              block.hashMerkleRoot.GetHex(),
              block.hashMerkleRoot_PoW.GetHex(),
              block.roundNumber,
              block.nBits,
              block.nTime,
              toHex(block.nSolution));
}

}

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
    dposBlockHash.SetNull();
    roundNumber = 0;
    tipBlockHash.SetNull();
    progenitorBlockHash.SetNull();
    authSignature.clear();
}

uint256 CTransactionVote::GetHash() const
{
    return SerializeHash(*this);
}

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
    dposBlockHash.SetNull();
    roundNumber = 0;
    tipBlockHash.SetNull();
    progenitorBlockHash.SetNull();
    authSignature.clear();
}

uint256 CProgenitorVote::GetHash() const
{
    return SerializeHash(*this);
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
                progenitorVotes[pair.second.dposBlockHash]++;
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

    const CKey operKey{extractOperatorKey()};
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

const CBlock* CProgenitorVoteTracker::findProgenitorBlock(const uint256 &dposBlcokHash)
{
    for (const auto& pair: *this->recievedVotes) {
        if (pair.second.dposBlockHash == dposBlcokHash) {
            return CProgenitorBlockTracker::getInstance().getReceivedBlock(pair.second.progenitorBlockHash);
        }
    }
    return nullptr;
}

bool CProgenitorVoteTracker::checkVoteIsConvenient(const CProgenitorVote &vote)
{
    return vote.tipBlockHash == chainActive.Tip()->GetBlockHash() &&
           validationListener_.progenitorBlocks.find(vote.progenitorBlockHash) != validationListener_.progenitorBlocks.end();
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

    if (checkProgenitorBlockIsConvenient(block)) {
        LockGuard lock{mutex_};
        libsnark::UNUSED(lock);
        rv = this->recievedBlocks->emplace(block.GetHash(), block).second;
    }

    if (rv) {
        voteForProgenitorBlock(block, extractOperatorKey());
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


const CProgenitorVote* CProgenitorBlockTracker::findMyVote(const CKey& key)
{
    LockGuard lock{mutex_};
    libsnark::UNUSED(lock);

    for (const auto& vote: CProgenitorVoteTracker::getInstance().listReceivedVotes()) {
        CPubKey pubKey{};
        CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
        ss << vote.roundNumber
           << vote.dposBlockHash
           << vote.tipBlockHash
           << vote.progenitorBlockHash
           << salt;
        if (pubKey.RecoverCompact(Hash(ss.begin(), ss.end()), vote.authSignature)) {
            if (pubKey == key.GetPubKey()) {
                return &vote;
            }
        }

    }
    return nullptr;
}

bool CProgenitorBlockTracker::voteForProgenitorBlock(const CBlock& progenitorBlock, const CKey& operatorKey)
{
    if (operatorKey.IsValid() && findMyVote(operatorKey) == nullptr) {
        CProgenitorVote vote{};
        CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
        CBlock dposBlock{tranformProgenitorBlock(progenitorBlock)};

        vote.roundNumber = progenitorBlock.roundNumber;
        vote.dposBlockHash = dposBlock.GetHash();
        vote.tipBlockHash = progenitorBlock.hashPrevBlock;
        vote.progenitorBlockHash = progenitorBlock.GetHash();
        vote.authSignature.resize(CPubKey::COMPACT_SIGNATURE_SIZE);

        ss << vote.roundNumber
           << vote.dposBlockHash
           << vote.tipBlockHash
           << vote.progenitorBlockHash
           << salt;

        if (operatorKey.SignCompact(Hash(ss.begin(), ss.end()), vote.authSignature)) {
            CProgenitorVoteTracker::getInstance().post(vote);
        } else {
            LogPrintf("%s: Can't vote for pre-block %s", __func__, progenitorBlock.GetHash().GetHex());
        }
    }
}

bool dpos::checkIsActive()
{
    const CChainParams& params{Params()};
    return NetworkUpgradeActive(chainActive.Height(), params.GetConsensus(), Consensus::UPGRADE_SAPLING) &&
           pmasternodesview->activeNodes.size() >= params.GetMinimalMasternodeCount();
}

CValidationInterface* dpos::getValidationListener()
{
    return &validationListener_;
}
