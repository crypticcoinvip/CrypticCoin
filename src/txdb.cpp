// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb.h"
#include "masternodes/masternodes.h"
#include "masternodes/dpos_p2p_messages.h"

#include "chainparams.h"
#include "hash.h"
#include "main.h"
#include "pow.h"
#include "uint256.h"

#include <stdint.h>

#include <boost/thread.hpp>

using namespace std;

// NOTE: Per issue #3277, do not use the prefix 'X' or 'x' as they were
// previously used by DB_SAPLING_ANCHOR and DB_BEST_SAPLING_ANCHOR.

// Prefixes for the coin database (chainstate/)
static const char DB_SPROUT_ANCHOR = 'A';
static const char DB_SAPLING_ANCHOR = 'Z';
static const char DB_NULLIFIER = 's';
static const char DB_SAPLING_NULLIFIER = 'S';
static const char DB_COINS = 'c';
static const char DB_BEST_BLOCK = 'B';
static const char DB_BEST_SPROUT_ANCHOR = 'a';
static const char DB_BEST_SAPLING_ANCHOR = 'z';

// Prefixes to the block database (blocks/index/)
static const char DB_BLOCK_FILES = 'f';
static const char DB_TXINDEX = 't';
static const char DB_BLOCK_INDEX = 'b';
static const char DB_FLAG = 'F';
static const char DB_REINDEX_FLAG = 'R';
static const char DB_LAST_BLOCK = 'l';

// Prefixes to the masternodes database (masternodes/)
static const char DB_MASTERNODES = 'M';
static const char DB_MASTERNODESUNDO = 'U';
static const char DB_SETOPERATORUNDO = 'u';
static const char DB_DISMISSVOTES = 'V';
static const char DB_TEAM = 'T';

// Prefixes to the dpos database (dpos/)
static const char DB_DPOS_TX_VOTES = 't';
static const char DB_DPOS_ROUND_VOTES = 'p';
static const char DB_DPOS_VICE_BLOCKS = 'b';

namespace
{
    template<typename Key, typename Value>
    void dbWrite(CDBWrapper * db, Key key, Value value, CDBBatch * batch, bool fsync = false)
    {
        assert(db != nullptr);

        if (batch == nullptr) {
            db->Write(key, value, fsync);
        } else {
            batch->Write(key, value);
        }
    }

    template<typename Key>
    void dbErase(CDBWrapper * db, Key key, CDBBatch * batch, bool fsync = false)
    {
        assert(db != nullptr);

        if (batch == nullptr) {
            db->Erase(key, fsync);
        } else {
            batch->Erase(key);
        }
    }
}

CCoinsViewDB::CCoinsViewDB(std::string dbName, size_t nCacheSize, bool fMemory, bool fWipe) : db(GetDataDir() / dbName, nCacheSize, fMemory, fWipe) {
}

CCoinsViewDB::CCoinsViewDB(size_t nCacheSize, bool fMemory, bool fWipe) : db(GetDataDir() / "chainstate", nCacheSize, fMemory, fWipe) 
{
}

bool CCoinsViewDB::GetSproutAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const {
    if (rt == SproutMerkleTree::empty_root()) {
        SproutMerkleTree new_tree;
        tree = new_tree;
        return true;
    }

    bool read = db.Read(make_pair(DB_SPROUT_ANCHOR, rt), tree);

    return read;
}

bool CCoinsViewDB::GetSaplingAnchorAt(const uint256 &rt, SaplingMerkleTree &tree) const {
    if (rt == SaplingMerkleTree::empty_root()) {
        SaplingMerkleTree new_tree;
        tree = new_tree;
        return true;
    }

    bool read = db.Read(make_pair(DB_SAPLING_ANCHOR, rt), tree);

    return read;
}

bool CCoinsViewDB::GetNullifier(const uint256 &nf, ShieldedType type) const {
    bool spent = false;
    char dbChar;
    switch (type) {
        case SPROUT:
            dbChar = DB_NULLIFIER;
            break;
        case SAPLING:
            dbChar = DB_SAPLING_NULLIFIER;
            break;
        default:
            throw runtime_error("Unknown shielded type");
    }
    return db.Read(make_pair(dbChar, nf), spent);
}

bool CCoinsViewDB::GetCoins(const uint256 &txid, CCoins &coins) const {
    return db.Read(make_pair(DB_COINS, txid), coins);
}

bool CCoinsViewDB::HaveCoins(const uint256 &txid) const {
    return db.Exists(make_pair(DB_COINS, txid));
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!db.Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

uint256 CCoinsViewDB::GetBestAnchor(ShieldedType type) const {
    uint256 hashBestAnchor;
    
    switch (type) {
        case SPROUT:
            if (!db.Read(DB_BEST_SPROUT_ANCHOR, hashBestAnchor))
                return SproutMerkleTree::empty_root();
            break;
        case SAPLING:
            if (!db.Read(DB_BEST_SAPLING_ANCHOR, hashBestAnchor))
                return SaplingMerkleTree::empty_root();
            break;
        default:
            throw runtime_error("Unknown shielded type");
    }

    return hashBestAnchor;
}

void BatchWriteNullifiers(CDBBatch& batch, CNullifiersMap& mapToUse, const char& dbChar)
{
    for (CNullifiersMap::iterator it = mapToUse.begin(); it != mapToUse.end();) {
        if (it->second.flags & CNullifiersCacheEntry::DIRTY) {
            if (!it->second.entered)
                batch.Erase(make_pair(dbChar, it->first));
            else
                batch.Write(make_pair(dbChar, it->first), true);
            // TODO: changed++? ... See comment in CCoinsViewDB::BatchWrite. If this is needed we could return an int
        }
        CNullifiersMap::iterator itOld = it++;
        mapToUse.erase(itOld);
    }
}

template<typename Map, typename MapIterator, typename MapEntry, typename Tree>
void BatchWriteAnchors(CDBBatch& batch, Map& mapToUse, const char& dbChar)
{
    for (MapIterator it = mapToUse.begin(); it != mapToUse.end();) {
        if (it->second.flags & MapEntry::DIRTY) {
            if (!it->second.entered)
                batch.Erase(make_pair(dbChar, it->first));
            else {
                if (it->first != Tree::empty_root()) {
                    batch.Write(make_pair(dbChar, it->first), it->second.tree);
                }
            }
            // TODO: changed++?
        }
        MapIterator itOld = it++;
        mapToUse.erase(itOld);
    }
}

bool CCoinsViewDB::BatchWrite(CCoinsMap &mapCoins,
                              const uint256 &hashBlock,
                              const uint256 &hashSproutAnchor,
                              const uint256 &hashSaplingAnchor,
                              CAnchorsSproutMap &mapSproutAnchors,
                              CAnchorsSaplingMap &mapSaplingAnchors,
                              CNullifiersMap &mapSproutNullifiers,
                              CNullifiersMap &mapSaplingNullifiers) {
    CDBBatch batch(db);
    size_t count = 0;
    size_t changed = 0;
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            if (it->second.coins.IsPruned())
                batch.Erase(make_pair(DB_COINS, it->first));
            else
                batch.Write(make_pair(DB_COINS, it->first), it->second.coins);
            changed++;
        }
        count++;
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }

    ::BatchWriteAnchors<CAnchorsSproutMap, CAnchorsSproutMap::iterator, CAnchorsSproutCacheEntry, SproutMerkleTree>(batch, mapSproutAnchors, DB_SPROUT_ANCHOR);
    ::BatchWriteAnchors<CAnchorsSaplingMap, CAnchorsSaplingMap::iterator, CAnchorsSaplingCacheEntry, SaplingMerkleTree>(batch, mapSaplingAnchors, DB_SAPLING_ANCHOR);

    ::BatchWriteNullifiers(batch, mapSproutNullifiers, DB_NULLIFIER);
    ::BatchWriteNullifiers(batch, mapSaplingNullifiers, DB_SAPLING_NULLIFIER);

    if (!hashBlock.IsNull())
        batch.Write(DB_BEST_BLOCK, hashBlock);
    if (!hashSproutAnchor.IsNull())
        batch.Write(DB_BEST_SPROUT_ANCHOR, hashSproutAnchor);
    if (!hashSaplingAnchor.IsNull())
        batch.Write(DB_BEST_SAPLING_ANCHOR, hashSaplingAnchor);

    LogPrint("coindb", "Committing %u changed transactions (out of %u) to coin database...\n", (unsigned int)changed, (unsigned int)count);
    return db.WriteBatch(batch);
}

bool CCoinsViewDB::GetStats(CCoinsStats &stats) const {
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&db)->NewIterator());
    pcursor->Seek(DB_COINS);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = GetBestBlock();
    ss << stats.hashBlock;
    CAmount nTotalAmount = 0;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        CCoins coins;
        if (pcursor->GetKey(key) && key.first == DB_COINS) {
            if (pcursor->GetValue(coins)) {
                stats.nTransactions++;
                for (unsigned int i=0; i<coins.vout.size(); i++) {
                    const CTxOut &out = coins.vout[i];
                    if (!out.IsNull()) {
                        stats.nTransactionOutputs++;
                        ss << VARINT(i+1);
                        ss << out;
                        nTotalAmount += out.nValue;
                    }
                }
                stats.nSerializedSize += 32 + pcursor->GetValueSize();
                ss << VARINT(0);
            } else {
                return error("CCoinsViewDB::GetStats() : unable to read value");
            }
        } else {
            break;
        }
        pcursor->Next();
    }
    {
        LOCK(cs_main);
        stats.nHeight = mapBlockIndex.find(stats.hashBlock)->second->nHeight;
    }
    stats.hashSerialized = ss.GetHash();
    stats.nTotalAmount = nTotalAmount;
    return true;
}

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe) {
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists(DB_REINDEX_FLAG);
    return true;
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) {
    return Read(DB_LAST_BLOCK, nFile);
}

bool CBlockTreeDB::WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<int, const CBlockFileInfo*> >::const_iterator it=fileInfo.begin(); it != fileInfo.end(); it++) {
        batch.Write(make_pair(DB_BLOCK_FILES, it->first), *it->second);
    }
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (std::vector<const CBlockIndex*>::const_iterator it=blockinfo.begin(); it != blockinfo.end(); it++) {
        batch.Write(make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()), CDiskBlockIndex(*it));
    }
    return WriteBatch(batch, true);
}

bool CBlockTreeDB::EraseBatchSync(const std::vector<const CBlockIndex*>& blockinfo) {
    CDBBatch batch(*this);
    for (std::vector<const CBlockIndex*>::const_iterator it=blockinfo.begin(); it != blockinfo.end(); it++) {
        batch.Erase(make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()));
    }
    return WriteBatch(batch, true);
}

bool CBlockTreeDB::ReadTxIndex(const uint256 &txid, CDiskTxPos &pos) {
    return Read(make_pair(DB_TXINDEX, txid), pos);
}

bool CBlockTreeDB::WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<uint256,CDiskTxPos> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_TXINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::WriteFlag(const std::string &name, bool fValue) {
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const std::string &name, bool &fValue) {
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts()
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_BLOCK_INDEX, uint256()));

    // Load mapBlockIndex
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_BLOCK_INDEX) {
            CDiskBlockIndex diskindex;
            if (pcursor->GetValue(diskindex)) {
                // Construct block index object
                CBlockIndex* pindexNew = InsertBlockIndex(diskindex.GetBlockHash());
                pindexNew->pprev          = InsertBlockIndex(diskindex.hashPrev);
                pindexNew->nHeight        = diskindex.nHeight;
                pindexNew->nFile          = diskindex.nFile;
                pindexNew->nDataPos       = diskindex.nDataPos;
                pindexNew->nUndoPos       = diskindex.nUndoPos;
                pindexNew->hashSproutAnchor     = diskindex.hashSproutAnchor;
                pindexNew->nVersion       = diskindex.nVersion;
                pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
                pindexNew->hashFinalSaplingRoot   = diskindex.hashFinalSaplingRoot;
                pindexNew->nTime          = diskindex.nTime;
                pindexNew->nBits          = diskindex.nBits;
                pindexNew->nNonce         = diskindex.nNonce;
                pindexNew->nSolution      = diskindex.nSolution;
                pindexNew->hashReserved1  = diskindex.hashReserved1;
                pindexNew->hashReserved2  = diskindex.hashReserved2;
                pindexNew->nRound         = diskindex.nRound;
                pindexNew->nStatus        = diskindex.nStatus;
                pindexNew->nCachedBranchId = diskindex.nCachedBranchId;
                pindexNew->nTx            = diskindex.nTx;
                pindexNew->nSproutValue   = diskindex.nSproutValue;
                pindexNew->nSaplingValue  = diskindex.nSaplingValue;

                // Consistency checks
                auto header = pindexNew->GetBlockHeader();
                if (header.GetHash() != pindexNew->GetBlockHash())
                    return error("LoadBlockIndex(): block header inconsistency detected: on-disk = %s, in-memory = %s",
                       diskindex.ToString(),  pindexNew->ToString());
                if (!CheckProofOfWork(pindexNew->GetBlockHash(), pindexNew->nBits, Params().GetConsensus()))
                    return error("LoadBlockIndex(): CheckProofOfWork failed: %s", pindexNew->ToString());

                pcursor->Next();
            } else {
                return error("LoadBlockIndex() : failed to read value");
            }
        } else {
            break;
        }
    }

    return true;
}


CMasternodesDB::CMasternodesDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : db(new CDBWrapper(GetDataDir() / "masternodes", nCacheSize, fMemory, fWipe))
    , readOnly(false)
{
}

CMasternodesDB::CMasternodesDB(const CMasternodesDB & other)
    : db(other.db)
    , readOnly(true)
{
}

void CMasternodesDB::CommitBatch()
{
    if (batch && !readOnly)
    {
        db->WriteBatch(*batch);
        batch.reset();
    }
}

void CMasternodesDB::DropBatch()
{
    if (batch && !readOnly)
    {
        batch.reset();
    }
}

void CMasternodesDB::WriteMasternode(uint256 const & txid, CMasternode const & node)
{
    BatchWrite(make_pair(DB_MASTERNODES, txid), node);
}

void CMasternodesDB::EraseMasternode(uint256 const & txid)
{
    BatchErase(make_pair(DB_MASTERNODES, txid));
}

void CMasternodesDB::WriteVote(uint256 const & txid, CDismissVote const & vote)
{
    BatchWrite(make_pair(DB_DISMISSVOTES, txid), vote);
}

void CMasternodesDB::EraseVote(uint256 const & txid)
{
    BatchErase(make_pair(DB_DISMISSVOTES, txid));
}

void CMasternodesDB::WriteUndo(uint256 const & txid, uint256 const & affectedItem, char undoType)
{
    BatchWrite(make_pair(make_pair(DB_MASTERNODESUNDO, txid), affectedItem), undoType);
}

void CMasternodesDB::EraseUndo(uint256 const & txid, uint256 const & affectedItem)
{
    BatchErase(make_pair(make_pair(DB_MASTERNODESUNDO, txid), affectedItem));
}

void CMasternodesDB::ReadOperatorUndo(const uint256 & txid, CMasternodesView::COperatorUndoRec & value)
{
    db->Read(make_pair(DB_SETOPERATORUNDO, txid), value);
}

void CMasternodesDB::WriteOperatorUndo(uint256 const & txid, CMasternodesView::COperatorUndoRec const & value)
{
    BatchWrite(make_pair(DB_SETOPERATORUNDO, txid), value);
}

void CMasternodesDB::EraseOperatorUndo(uint256 const & txid)
{
    BatchErase(make_pair(DB_SETOPERATORUNDO, txid));
}

bool CMasternodesDB::ReadTeam(int blockHeight, CTeam & team) const
{
    team.clear();
    boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
    pcursor->Seek(make_pair(DB_TEAM, blockHeight));

    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::pair<char, int>, uint256> key;
        if (pcursor->GetKey(key) && key.first.first == DB_TEAM && key.first.second == blockHeight)
        {
            std::pair<int32_t, CKeyID> value;
            if (pcursor->GetValue(value))
            {
                team.insert(make_pair(key.second, value));
            }
            else
            {
                return error("CMasternodesDB::ReadTeam() : unable to read value");
            }
        }
        else
        {
            break;
        }
        pcursor->Next();
    }
    return true;
}

bool CMasternodesDB::WriteTeam(int blockHeight, CTeam const & team)
{
    if (readOnly)
    {
        return true;
    }
    // To enshure that we have no any mismatches in particular records
    bool erased = EraseTeam(blockHeight);

    /// @attention EraseTeam() and WriteTeam() uses their own batches
    /// cause i'm not sure that 'erasing' and then 'writing' in one batch will lead to the expected result
    CDBBatch batch(*db);
    for (CTeam::const_iterator it = team.begin(); it != team.end(); ++it)
    {
        batch.Write(make_pair(make_pair(DB_TEAM, blockHeight), it->first), it->second);
    }
    return erased && db->WriteBatch(batch);
}

bool CMasternodesDB::EraseTeam(int blockHeight)
{
    if (readOnly)
    {
        return true;
    }
    /// @attention EraseTeam() and WriteTeam() uses their own batches
    /// cause i'm not sure that 'erasing' and then 'writing' in one batch will lead to the expected result
    CDBBatch batch(*db);
    boost::scoped_ptr<CDBIterator> pcursor(db->NewIterator());
    pcursor->Seek(make_pair(DB_TEAM, blockHeight));

    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::pair<char, int>, uint256> key;
        if (pcursor->GetKey(key) && key.first.first == DB_TEAM && key.first.second == blockHeight)
        {
            batch.Erase(make_pair(make_pair(DB_TEAM, blockHeight), key.second));
        }
        else
        {
            break;
        }
        pcursor->Next();
    }
    return db->WriteBatch(batch);
}


bool CMasternodesDB::LoadMasternodes(std::function<void(uint256 &, CMasternode &)> onNode) const
{
    boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
    pcursor->Seek(DB_MASTERNODES);

    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_MASTERNODES)
        {
            CMasternode node;
            if (pcursor->GetValue(node))
            {
                onNode(key.second, node);
            }
            else
            {
                return error("CMasternodesDB::LoadMasternodes() : unable to read value");
            }
        }
        else
        {
            break;
        }
        pcursor->Next();
    }
    return true;
}

bool CMasternodesDB::LoadVotes(std::function<void(uint256 &, CDismissVote &)> onVote) const
{
    boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
    pcursor->Seek(DB_DISMISSVOTES);

    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        if (pcursor->GetKey(key) && key.first == DB_DISMISSVOTES)
        {
            CDismissVote vote;
            if (pcursor->GetValue(vote))
            {
                onVote(key.second, vote);
            }
            else
            {
                return error("CMasternodesDB::LoadVotes() : unable to read value");
            }
        }
        else
        {
            break;
        }
        pcursor->Next();
    }
    return true;
}

bool CMasternodesDB::LoadUndo(std::function<void(uint256 &, uint256 &, char)> onUndo) const
{
    boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
    pcursor->Seek(DB_MASTERNODESUNDO);

    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        std::pair<std::pair<char, uint256>, uint256> key;
        if (pcursor->GetKey(key) && key.first.first == DB_MASTERNODESUNDO)
        {
//            batch.Write(make_pair(make_pair(DB_MASTERNODESUNDO, txid), affectedNode), undoType);
            char undoType;
            if (pcursor->GetValue(undoType))
            {
                onUndo(key.first.second, key.second, undoType);
            }
            else
            {
                return error("CMasternodesDB::LoadUndo() : unable to read value");
            }
        }
        else
        {
            break;
        }
        pcursor->Next();
    }
    return true;
}



CDposDB::CDposDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    CDBWrapper(GetDataDir() / "dpos", nCacheSize, fMemory, fWipe)
{
}

void CDposDB::WriteViceBlock(const uint256& tip, const CBlock& block, CDBBatch* batch)
{
    dbWrite(this, make_pair(DB_DPOS_VICE_BLOCKS, tip), block, batch);
}


void CDposDB::WriteRoundVote(const uint256& tip, const dpos::CRoundVote_p2p& vote, CDBBatch* batch)
{
    dbWrite(this, make_pair(DB_DPOS_ROUND_VOTES, tip), vote, batch);
}

void CDposDB::WriteTxVote(const uint256& tip, const dpos::CTxVote_p2p& vote, CDBBatch* batch)
{
    dbWrite(this, make_pair(DB_DPOS_TX_VOTES, tip), vote, batch);
}

void CDposDB::EraseViceBlock(const uint256& tip, CDBBatch* batch)
{
    dbErase(this, make_pair(DB_DPOS_VICE_BLOCKS, tip), batch);
}

void CDposDB::EraseRoundVote(const uint256& tip, CDBBatch* batch)
{
    dbErase(this, make_pair(DB_DPOS_ROUND_VOTES, tip), batch);
}

void CDposDB::EraseTxVote(const uint256& tip, CDBBatch* batch)
{
    dbErase(this, make_pair(DB_DPOS_TX_VOTES, tip), batch);
}

bool CDposDB::LoadViceBlocks(std::function<void (const uint256&, const CBlock&)> onViceBlock)
{
    boost::scoped_ptr<CDBIterator> pcursor{NewIterator()};
    pcursor->Seek(DB_DPOS_VICE_BLOCKS);

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key{};
        if (pcursor->GetKey(key) && key.first == DB_DPOS_VICE_BLOCKS) {
            CBlock block{};
            if (pcursor->GetValue(block)) {
                onViceBlock(key.second, block);
            } else {
                return error("CDposDB::LoadViceBlocks() : unable to read value");
            }
        } else {
            break;
        }
        pcursor->Next();
    }
    return true;
}

bool CDposDB::LoadRoundVotes(std::function<void (const uint256&, const dpos::CRoundVote_p2p&)> onRoundVote)
{
    boost::scoped_ptr<CDBIterator> pcursor{NewIterator()};
    pcursor->Seek(DB_DPOS_ROUND_VOTES);

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key{};
        if (pcursor->GetKey(key) && key.first == DB_DPOS_ROUND_VOTES) {
            dpos::CRoundVote_p2p vote{};
            if (pcursor->GetValue(vote)) {
                onRoundVote(key.second, vote);
            } else {
                return error("CDposDB::LoadRoundVotes() : unable to read value");
            }
        } else {
            break;
        }
        pcursor->Next();
    }
    return true;
}

bool CDposDB::LoadTxVotes(std::function<void (const uint256&, const dpos::CTxVote_p2p&)> onTxVote)
{
    boost::scoped_ptr<CDBIterator> pcursor{NewIterator()};
    pcursor->Seek(DB_DPOS_TX_VOTES);

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key{};
        if (pcursor->GetKey(key) && key.first == DB_DPOS_TX_VOTES) {
            dpos::CTxVote_p2p vote{};
            if (pcursor->GetValue(vote)) {
                onTxVote(key.second, vote);
            } else {
                return error("CDposDB::LoadTxVotes() : unable to read value");
            }
        } else {
            break;
        }
        pcursor->Next();
    }
    return true;
}
