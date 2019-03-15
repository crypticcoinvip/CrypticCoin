// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "../rpc/server.h"

#include "../consensus/upgrades.h"
#include "../init.h"
#include "../key_io.h"
#include "../main.h"
#include "masternodes.h"
#include "../rpc/server.h"
#include "../script/script_error.h"
#include "../script/sign.h"
#include "../version.h"
#ifdef ENABLE_WALLET
#include "../wallet/wallet.h"
#endif

#include <stdexcept>
#include <univalue.h>

#include <boost/assign/list_of.hpp>

extern UniValue createrawtransaction(UniValue const & params, bool fHelp); // in rawtransaction.cpp
extern UniValue fundrawtransaction(UniValue const & params, bool fHelp); // in rpcwallet.cpp
extern UniValue signrawtransaction(UniValue const & params, bool fHelp); // in rawtransaction.cpp
extern UniValue sendrawtransaction(UniValue const & params, bool fHelp); // in rawtransaction.cpp
extern UniValue getnewaddress(UniValue const & params, bool fHelp); // in rpcwallet.cpp
extern bool EnsureWalletIsAvailable(bool avoidException); // in rpcwallet.cpp
extern bool DecodeHexTx(CTransaction & tx, std::string const & strHexTx); // in core_io.h
extern std::string EncodeHexTx(CTransaction const & tx);

extern void ScriptPubKeyToJSON(CScript const & scriptPubKey, UniValue & out, bool fIncludeHex); // in rawtransaction.cpp

void EnsureSaplingUpgrade()
{
    if (!NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_SAPLING))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Sapling upgrade was not activated!");
    }
}

UniValue RawCreateFundSignSend(UniValue params, std::string const & linkedTxHex = std::string(), CKeyID const & changeAddress = CKeyID())
{
// 1. Create
    UniValue created = createrawtransaction(params, false);

// 2. Fund. Copy-pasted implementation of 'fundrawtransaction', cause we whatever need to decode for change rearrange
    EnsureWalletIsAvailable(false);
    CTransaction origTx;
    if (!DecodeHexTx(origTx, created.get_str()))
    {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }
    CMutableTransaction tx(origTx);
    int voutsSize = tx.vout.size();
    CAmount nFee;
    std::string strFailReason;
    int nChangePos = -1;
    if(!pwalletMain->FundTransaction(tx, nFee, nChangePos, strFailReason))
    {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);
    }
    // Rearrange autocreated change position if it exists and not at the end
    if (nChangePos != -1 && nChangePos != voutsSize)
    {
        auto it = tx.vout.begin() + nChangePos;
        std::rotate(it, it + 1, tx.vout.end());
    }
    if (changeAddress != CKeyID() && nChangePos != -1)
    {
        // Pick 'autocreated' change for our changeAddress (we have change, and it is totally ours)
        tx.vout.back().scriptPubKey = GetScriptForDestination(changeAddress);
    }

// 3. Sign
    UniValue signparams(UniValue::VARR);
    signparams.push_back(EncodeHexTx(tx));
    if (!linkedTxHex.empty())
    {
        CTransaction linkedTx;
        if (!DecodeHexTx(linkedTx, linkedTxHex))
        {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "MN: error decoding auth tx");
        }
        UniValue prevtx(UniValue::VOBJ);
        prevtx.push_back(Pair("txid", linkedTx.GetHash().GetHex()));
        prevtx.push_back(Pair("vout", 0));
        prevtx.push_back(Pair("scriptPubKey", HexStr(linkedTx.vout[0].scriptPubKey.begin(), linkedTx.vout[0].scriptPubKey.end())));
//        prevtxs.push_back(Pair("redeemScript", authTx.GetHash().GetHex()));
        prevtx.push_back(Pair("amount", ValueFromAmount(linkedTx.vout[0].nValue)));
        UniValue prevtxarray(UniValue::VARR);
        prevtxarray.push_back(prevtx);
        signparams.push_back(prevtxarray);
    }
    UniValue signedTxObj = signrawtransaction(signparams, false);
    /* returns {
    "hex": "010000000...",
    "complete": true
    } */

// 4. Send
    UniValue sendparams(UniValue::VARR);
    sendparams.push_back(signedTxObj["hex"]);
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("txid", sendrawtransaction(sendparams, false)));
    result.push_back(Pair("hex", signedTxObj["hex"].getValStr()));
    return result;
}


/*
 * 'Wrapper' in the name is only to distinguish it from the rest 'Accesses'
*/
CCoins const * AccessCoinsWrapper(uint256 const & txid)
{
    // Just for now, use the easiest and clean way to get coins
    return pcoinsTip->AccessCoins(txid);

    /// @todo @mn Investigate the magic of mempool|view switching from original 'signrawtransaction'
    /// or simplified from 'gettxout'.

    /// from 'signrawtransaction':
    /*
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        BOOST_FOREACH(const CTxIn& txin, mergedTx.vin) {
            const uint256& prevHash = txin.prevout.hash;
            CCoins coins;
            view.AccessCoins(prevHash); // this is certainly allowed to fail
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }
    */
    /// from 'gettxout':
//    LOCK(mempool.cs);
//    CCoinsViewMemPool view(pcoinsTip, mempool);
}


/*
 * If inputs are not empty, matches first input to the given auth and fails if not match.
 * If empty - looks for coins (at least one UTXO) for given auth.
 * If still empty - creates own "preauth" tx with minimal funds
 * @returns: Updated inputs and raw hex of "preauth" tx (if any)
*/
std::string ProvideAuth(CKeyID const & auth, UniValue & inputs)
{
    if (inputs.size() > 0)
    {
        // #1. check first input
        UniValue const & input = inputs[0];
        UniValue const & o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        }
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");
        }

        CCoins const * coins = AccessCoinsWrapper(txid);
        if (!coins || !coins->IsAvailable(nOutput))
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Input not found or already spent");
        }
        CScript const & prevPubKey = coins->vout[nOutput].scriptPubKey;
        CTxDestination address;
        ExtractDestination(prevPubKey, address);
        CKeyID const * inputAuth = boost::get<CKeyID>(&address);
        if (!inputAuth || *inputAuth != auth)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Check of authentication failed");
        }
    }
    else
    {
        // #2. search for just one UTXO matching 'auth'
        std::vector<COutput> vecOutputs;
        assert(pwalletMain != NULL);
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->AvailableCoins(vecOutputs, true, NULL, false, false);

        BOOST_FOREACH(const COutput& out, vecOutputs)
        {
            CTxDestination address;
            CScript const & scriptPubKey = out.tx->vout[out.i].scriptPubKey;
            bool fValidAddress = ExtractDestination(scriptPubKey, address);
            if (!fValidAddress)
                continue;
            CKeyID const * inputAuth = boost::get<CKeyID>(&address);
            if (inputAuth && *inputAuth == auth)
            {
                UniValue entry(UniValue::VOBJ);
                entry.push_back(Pair("txid", out.tx->GetHash().GetHex()));
                entry.push_back(Pair("vout", out.i));
                inputs.push_back(entry);
                break;
            }
        }
        if (inputs.size() == 0)
        {
            // #3. Create auth funding transaction (fund with DustTrashold amount)
            CTxOut authOut(CAmount(1), GetScriptForDestination(auth));
            CAmount dustThreshold = authOut.GetDustThreshold(::minRelayTxFee);

            UniValue vouts(UniValue::VOBJ);
            vouts.push_back(Pair(EncodeDestination(auth), ValueFromAmount(dustThreshold)));

            UniValue params(UniValue::VARR);
            params.push_back(UniValue(UniValue::VARR));
            params.push_back(vouts);

            UniValue result = RawCreateFundSignSend(params);
            LogPrintf("MN: autocreate auth funding tx: %s\n", result["txid"].getValStr());

            // Update original inputs
            UniValue entry(UniValue::VOBJ);
            entry.push_back(Pair("txid", result["txid"]));
            entry.push_back(Pair("vout", 0)); // it is in 0 position, cause we always rearrange change
            inputs.push_back(entry);

            return result["hex"].getValStr();
        }
    }
    return "";
}

/*
 *
 *  Issued by: any
*/
UniValue mn_announce(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw std::runtime_error(
            "mn_announce [{\"txid\":\"id\",\"vout\":n},...] {\"address\":amount,...}\n"
            "\nCreates (and submits to local node and network) a masternode announcement transaction with given metadata, spending the given inputs.\n"
            "\nArguments:\n"
            "1. \"transactions\"        (string, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",  (string, required) The transaction id\n"
            "         \"vout\":n        (numeric, required) The output number\n"
            "         \"sequence\":n    (numeric, optional) The sequence number\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"
            "2. \"metadata\"           (string, required) a json object with masternode metadata keys and values\n"
            "    {\n"
            "      \"name\": name                        (string, required) Masternode human-friendly name, should be at least size 3 and less than 255\n"
            "      \"ownerAuthAddress\": P2PKH           (string, required) Masternode owner auth address (P2PKH only, unique)\n"
            "      \"operatorAuthAddress\": P2PKH        (string, required) Masternode operator auth address (P2PKH only, unique)\n"
            "      \"ownerRewardAddress\": P2PKH or P2SH (string, required) Masternode owner reward address (any P2PKH or P2SH address)\n"
            "      \"collateralAddress\": P2PKH or P2SH  (string, required) Any valid address for keeping collateral amount (any P2PKH or P2SH address)\n"
            "    }\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
//            "\nExamples\n"
//            + HelpExampleCli("mn_announce", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"")
//            + HelpExampleRpc("mn_announce", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"address\\\":0.01}\"")
        );
    EnsureSaplingUpgrade();

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ), true);
    if (params[0].isNull() || params[1].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null, and argument 2 expected as object with "
                                                  "{\"name\",\"ownerAuthAddress\",\"operatorAuthAddress\",\"ownerRewardAddress\",\"collateralAddress\"}");
    }
    UniValue metaObj = params[1].get_obj();
    RPCTypeCheckObj(metaObj, boost::assign::map_list_of
                    ("name",                UniValue::VSTR)
                    ("ownerAuthAddress",    UniValue::VSTR)
                    ("operatorAuthAddress", UniValue::VSTR)
                    ("ownerRewardAddress",  UniValue::VSTR)
                    );
    std::string const name =                      metaObj["name"].getValStr();
    std::string const ownerAuthAddressBase58 =    metaObj["ownerAuthAddress"].getValStr();
    std::string const operatorAuthAddressBase58 = metaObj["operatorAuthAddress"].getValStr();
    std::string const ownerRewardAddress =        metaObj["ownerRewardAddress"].getValStr();

    std::string const operatorRewardAddress =     metaObj["operatorRewardAddress"].getValStr().empty() ? ownerRewardAddress : metaObj["operatorRewardAddress"].getValStr();
    int32_t     const operatorRewardRatio =       metaObj["operatorRewardRatio"].isNull() ? 0 : AmountFromValue(metaObj["operatorRewardRatio"]) * MN_BASERATIO / COIN;
    std::string       collateralAddress =         metaObj["collateralAddress"].getValStr();


    // Parameters validation block
    if (name.size() < 3 || name.size() >= 31)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, 'name' should be at least size 3 and not greater than 31");
    }

    // owner and operator check
    if (ownerAuthAddressBase58.empty() || operatorAuthAddressBase58.empty())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "ownerAuthAddress or operatorAuthAddress is empty");
    }
    if (ownerAuthAddressBase58 == operatorAuthAddressBase58)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "ownerAuthAddress and operatorAuthAddress must be different!");
    }
    CTxDestination destOwner = DecodeDestination(ownerAuthAddressBase58);
    CKeyID const * ownerAuthAddress = boost::get<CKeyID>(&destOwner);
    if (!ownerAuthAddress || ownerAuthAddress->IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "ownerAuthAddress does not refer to a PK2H address");
    }
    if (pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOwner, *ownerAuthAddress) ||
        pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOperator, *ownerAuthAddress))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode with ownerAuthAddress already exists");
    }
    CTxDestination destOperator = DecodeDestination(operatorAuthAddressBase58);
    CKeyID const * operatorAuthAddress = boost::get<CKeyID>(&destOperator);
    if (!operatorAuthAddress || operatorAuthAddress->IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorAuthAddress does not refer to a PK2H address");
    }
    if (pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOwner, *operatorAuthAddress) ||
        pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOperator, *operatorAuthAddress))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode with operatorAuthAddress already exists");
    }

    /// @todo @mn if we generate collateralAddress, we should report about it in out result
//    if (collateralAddress.empty())
//    {
//        collateralAddress = getnewaddress(UniValue(), false).getValStr();
//    }
    CTxDestination collateralDest = DecodeDestination(collateralAddress);
    if (collateralDest.which() != 1 && collateralDest.which() != 2)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "collateralAddress does not refer to a P2PKH or P2SH address");
    }
    CTxDestination ownerRewardDest = DecodeDestination(ownerRewardAddress);
    if (ownerRewardDest.which() != 1 && ownerRewardDest.which() != 2)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "ownerRewardAddress does not refer to a P2PKH or P2SH address");
    }
    // Optional (=ownerRewardAddress if empty)
    CTxDestination operatorRewardDest = DecodeDestination(operatorRewardAddress);
    if (operatorRewardDest.which() != 1 && operatorRewardDest.which() != 2)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorRewardAddress does not refer to a P2PKH or P2SH address");
    }
    // Optional
    if (operatorRewardRatio < 0 || operatorRewardRatio > MN_BASERATIO)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorRewardRatio should be >= 0 and <= 1");
    }

    CDataStream metadata(MnTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(MasternodesTxType::AnnounceMasternode)
             << name << *ownerAuthAddress << *operatorAuthAddress
             << ToByteVector(GetScriptForDestination(ownerRewardDest))
             << ToByteVector(GetScriptForDestination(operatorRewardDest))
             << operatorRewardRatio;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    // Current height + (1 day blocks) to avoid rejection;
    CAmount const blockSubsidy = GetBlockSubsidy(chainActive.Height() + 1, Params().GetConsensus());
    int targetHeight = chainActive.Height() + 1 + (60 * 60 / Params().GetConsensus().nPowTargetSpacing);
    size_t targetMnCount = pmasternodesview->GetActiveMasternodes().size() < 4 ? 0 : pmasternodesview->GetActiveMasternodes().size() - 4;

    UniValue vouts(UniValue::VOBJ);
    vouts.push_back(Pair(EncodeDestination(CTxDestination(scriptMeta)), ValueFromAmount(GetMnAnnouncementFee(blockSubsidy, targetHeight, targetMnCount))));
    vouts.push_back(Pair(EncodeDestination(collateralDest), ValueFromAmount(GetMnCollateralAmount())));

    UniValue newparams(UniValue::VARR);
    newparams.push_back(params[0]);
    newparams.push_back(vouts);

    UniValue result = RawCreateFundSignSend(newparams);
    return result["txid"];
}

/*
 *
 *  Issued by: operator
*/
UniValue mn_activate(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error("@todo: Help"
    );
    EnsureSaplingUpgrade();

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR), true);

    auto ids = pmasternodesview->AmIOperator();
    if (!ids)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "You are not an operator!");
    }
    auto optNode = pmasternodesview->ExistMasternode(ids->id);
    assert(optNode); // after AmIOperator it should be true

    CMasternode const & node = *optNode;
    int chainHeight = chainActive.Height() + 1;
    if (node.activationTx != uint256())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string() + "Can't activate. MN was activated by " + node.activationTx.GetHex());
    }
    if (node.collateralSpentTx != uint256())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string() + "Can't activate. Collateral was spent by " + node.collateralSpentTx.GetHex());
    }
    if (node.dismissFinalizedTx != uint256())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string() + "Can't activate. MN was dismissed by voting " + node.dismissFinalizedTx.GetHex());
    }
    if (node.minActivationHeight > chainHeight)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Can't activate. Minimal activation height not reached (block %d)", node.minActivationHeight));
    }
    UniValue inputs = params[0].get_array();
    std::string authTxHex = ProvideAuth(node.operatorAuthAddress, inputs);

    CDataStream metadata(MnTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(MasternodesTxType::ActivateMasternode)
             << ids->id;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    UniValue vouts(UniValue::VOBJ);
    vouts.push_back(Pair(EncodeDestination(scriptMeta), ValueFromAmount(0)));

    UniValue newparams(UniValue::VARR);
    newparams.push_back(inputs);
    newparams.push_back(vouts);

    UniValue result = RawCreateFundSignSend(newparams, authTxHex);
    return result["txid"];
}

/*
 *
 *  Issued by: active operator
*/
UniValue mn_dismissvote(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw std::runtime_error("@todo: Help"
    );
    EnsureSaplingUpgrade();

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ), true);
    if (params[0].isNull() || params[1].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null, and argument 2 expected as object with "
                                                  "{\"against\":MN-id, \"reason_code\":N, \"reason_desc\":\"description\"}");
    }
    UniValue metaObj = params[1].get_obj();
    RPCTypeCheckObj(metaObj, boost::assign::map_list_of
                    ("against",             UniValue::VSTR)
                    ("reason_code",         UniValue::VNUM)
                    ("reason_desc",         UniValue::VSTR)
                    );

    std::string againstHex = metaObj["against"].getValStr();
    uint32_t reason_code = static_cast<uint32_t>(metaObj["reason_code"].get_int());
    std::string reason_desc = metaObj["reason_desc"].getValStr();

    uint256 against = uint256S(againstHex);
    if (!pmasternodesview->ExistMasternode(against))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Masternode %s does not exist", againstHex));
    }
    if (reason_desc.size() > 255)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "reason_desc tooo long (>255)!");
    }

    auto ids = pmasternodesview->AmIActiveOperator();
    if (!ids)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "You are not an active operator!");
    }
    auto optNode = pmasternodesview->ExistMasternode(ids->id);
    assert(optNode); // after AmIOperator it should be true

    CMasternode const & node = *optNode;

    // Checking votes from|against
    if (node.counterVotesFrom >= MAX_DISMISS_VOTES_PER_MN)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("You've reached MAX_DISMISS_VOTES_PER_MN! (%d)", MAX_DISMISS_VOTES_PER_MN));
    }
    if (pmasternodesview->ExistActiveVoteIndex(CMasternodesView::VoteIndex::From, ids->id, against))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Vote against %s already exists!", againstHex));
    }

    UniValue inputs = params[0].get_array();
    std::string authTxHex = ProvideAuth(node.operatorAuthAddress, inputs);

    CDataStream metadata(MnTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(MasternodesTxType::DismissVote)
             << against << reason_code << reason_desc;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    UniValue vouts(UniValue::VOBJ);
    vouts.push_back(Pair(EncodeDestination(scriptMeta), ValueFromAmount(0)));

    UniValue newparams(UniValue::VARR);
    newparams.push_back(inputs);
    newparams.push_back(vouts);

    UniValue result = RawCreateFundSignSend(newparams, authTxHex);
    return result["txid"];
}

/*
 *
 *  Issued by: active operator
*/
UniValue mn_dismissvoterecall(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw std::runtime_error("@todo: Help"
    );
    EnsureSaplingUpgrade();

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ), true);
    if (params[0].isNull() || params[1].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null, and argument 2 expected as object with "
                                                  "{\"against\":MN-id}");
    }
    UniValue metaObj = params[1].get_obj();
    RPCTypeCheckObj(metaObj, boost::assign::map_list_of
                    ("against",             UniValue::VSTR)
                    );

    std::string againstHex = metaObj["against"].getValStr();

    uint256 against = uint256S(againstHex);
    if (!pmasternodesview->ExistMasternode(against))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Masternode %s does not exist", againstHex));
    }

    auto ids = pmasternodesview->AmIActiveOperator();
    if (!ids)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "You are not an active operator!");
    }

    // Checking votes from|against
    if (!pmasternodesview->ExistActiveVoteIndex(CMasternodesView::VoteIndex::From, ids->id, against))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Vote against %s does not exists!", againstHex));
    }

    UniValue inputs = params[0].get_array();
    std::string authTxHex = ProvideAuth(ids->operatorAuthAddress, inputs);

    CDataStream metadata(MnTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(MasternodesTxType::DismissVoteRecall)
             << against;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    UniValue vouts(UniValue::VOBJ);
    vouts.push_back(Pair(EncodeDestination(scriptMeta), ValueFromAmount(0)));

    UniValue newparams(UniValue::VARR);
    newparams.push_back(inputs);
    newparams.push_back(vouts);

    UniValue result = RawCreateFundSignSend(newparams, authTxHex);
    return result["txid"];
}

/*
 *
 *  Issued by: any
*/
UniValue mn_finalizedismissvoting(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw std::runtime_error("@todo: Help"
    );
    EnsureSaplingUpgrade();

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ), true);
    if (params[0].isNull() || params[1].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null, and argument 2 expected as object with "
                                                  "{\"against\":MN-id}");
    }
    UniValue metaObj = params[1].get_obj();
    RPCTypeCheckObj(metaObj, boost::assign::map_list_of
                    ("against",             UniValue::VSTR)
                    );

    std::string againstHex = metaObj["against"].getValStr();

    uint256 against = uint256S(againstHex);
    auto optNode = pmasternodesview->ExistMasternode(against);
    if (!optNode)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Masternode %s does not exist", againstHex));
    }

    CMasternode const & node = *optNode;

    // Checking votes from|against
    if (node.counterVotesAgainst < pmasternodesview->GetMinDismissingQuorum())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Dismissing quorum not reached! (min quorum = %d, current votes = %d)", pmasternodesview->GetMinDismissingQuorum(), node.counterVotesAgainst));
    }

    UniValue inputs = params[0].get_array();
    // no need to check authentication nor special funding
//    ProvideAuthOfFirstInput(node.operatorAuthAddress, inputs);

    CDataStream metadata(MnTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(MasternodesTxType::FinalizeDismissVoting)
             << against;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    UniValue vouts(UniValue::VOBJ);
    vouts.push_back(Pair(EncodeDestination(scriptMeta), ValueFromAmount(0)));

    UniValue newparams(UniValue::VARR);
    newparams.push_back(inputs);
    newparams.push_back(vouts);

    UniValue result = RawCreateFundSignSend(newparams);
    return result["txid"];
}


/*
 *
 *  Issued by: owner
*/
UniValue mn_setoperator(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw std::runtime_error("@todo: Help"
    );
    EnsureSaplingUpgrade();

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ), true);
    if (params[0].isNull() || params[1].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null, and argument 2 expected as object with "
                                                  "{\"operatorAuthAddress\",\"operatorRewardAddress\",\"operatorRewardRatio\"}");
    }
    UniValue metaObj = params[1].get_obj();
    RPCTypeCheckObj(metaObj, boost::assign::map_list_of
                    ("operatorAuthAddress",   UniValue::VSTR)
                    ("operatorRewardAddress", UniValue::VSTR)
                    ("operatorRewardRatio",   UniValue::VNUM)
                    );

    std::string const operatorAuthAddressBase58 = metaObj["operatorAuthAddress"].getValStr();
    std::string const operatorRewardAddress =     metaObj["operatorRewardAddress"].getValStr();
    int32_t     const operatorRewardRatio =       metaObj["operatorRewardRatio"].isNull() ? 0 : AmountFromValue(metaObj["operatorRewardRatio"]) * MN_BASERATIO / COIN;

    auto ids = pmasternodesview->AmIOwner();
    if (!ids)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "You are not an owner!");
    }
    auto optNode = pmasternodesview->ExistMasternode(ids->id);
    assert(optNode); // after AmIOwner it should be true

    CMasternode const & node = *optNode;

    CTxDestination destOperator = DecodeDestination(operatorAuthAddressBase58);
    CKeyID const * operatorAuthAddress = boost::get<CKeyID>(&destOperator);
    if (!operatorAuthAddress || operatorAuthAddress->IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorAuthAddress does not refer to a PK2H address");
    }
    if (pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOwner, *operatorAuthAddress) ||
        (pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOperator, *operatorAuthAddress) && *operatorAuthAddress != ids->operatorAuthAddress))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode with operatorAuthAddress already exists");
    }

    // Optional
    CTxDestination operatorRewardDest = DecodeDestination(operatorRewardAddress);
    if (!operatorRewardAddress.empty() && operatorRewardDest.which() != 1 && operatorRewardDest.which() != 2)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorRewardAddress does not refer to a P2PKH or P2SH address");
    }
    // Optional
    if (operatorRewardRatio < 0 || operatorRewardRatio > MN_BASERATIO)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorRewardRatio should be >= 0 and <= 1");
    }


    UniValue inputs = params[0].get_array();
    std::string authTxHex = ProvideAuth(node.ownerAuthAddress, inputs);

    CDataStream metadata(MnTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(MasternodesTxType::SetOperatorReward)
             << *operatorAuthAddress << ToByteVector(GetScriptForDestination(operatorRewardDest)) << operatorRewardRatio;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    UniValue vouts(UniValue::VOBJ);
    vouts.push_back(Pair(EncodeDestination(scriptMeta), ValueFromAmount(0)));

    UniValue newparams(UniValue::VARR);
    newparams.push_back(inputs);
    newparams.push_back(vouts);

    UniValue result = RawCreateFundSignSend(newparams, authTxHex);
    return result["txid"];
}


UniValue mn_resign(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw std::runtime_error("@todo: Help"
    );
    EnsureSaplingUpgrade();

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VSTR), false);

    uint256 nodeId = uint256S(params[0].getValStr());
    auto optNode = pmasternodesview->ExistMasternode(nodeId);
    if (!optNode || optNode->collateralSpentTx != uint256())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Masternode %s does not exist", params[0].getValStr()));
    }
    if (optNode->collateralSpentTx != uint256())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Collateral for masternode %s was already spent by tx %s", params[0].getValStr(), optNode->collateralSpentTx.GetHex()));
    }

    CTxDestination dest = DecodeDestination(params[1].getValStr());
    if (dest.which() != 1 && dest.which() != 2)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Destination address does not refer to a P2PKH or P2SH address");
    }

    const unsigned int collateralIn = 1;
    CCoins const * coins = AccessCoinsWrapper(nodeId);
    // After our previous check of collateralSpentTx in MNDB, this should not happen!
    if (!coins || !coins->IsAvailable(collateralIn))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Collateral for masternode was already spent!");
    }
    CScript const & prevPubKey = coins->vout[collateralIn].scriptPubKey;

    // Simplified createrawtransaction
    int nextBlockHeight = chainActive.Height() + 1;
    CMutableTransaction rawTx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nextBlockHeight);

    uint32_t nSequence = (rawTx.nLockTime ? std::numeric_limits<uint32_t>::max() - 1 : std::numeric_limits<uint32_t>::max());
    CTxIn in(COutPoint(nodeId, 1), CScript(), nSequence);
    rawTx.vin.push_back(in);

    CTxOut out(GetMnCollateralAmount(), GetScriptForDestination(dest));
    rawTx.vout.push_back(out);
    // end-of-createrawtransaction

    // Calculate fee by dummy signature. Don't check its amount cause collateral is large enough
    {
        auto consensusBranchId = CurrentEpochBranchId(nextBlockHeight, Params().GetConsensus());
        SignatureData sigdata;
        ProduceSignature(DummySignatureCreator(pwalletMain), prevPubKey, sigdata, consensusBranchId);
        UpdateTransaction(rawTx, 0, sigdata);
        unsigned int nTxBytes = ::GetSerializeSize(rawTx, SER_NETWORK, PROTOCOL_VERSION);

        CAmount nFeeNeeded = CWallet::GetMinimumFee(nTxBytes, nTxConfirmTarget, mempool);

        rawTx.vout.back().nValue -= nFeeNeeded;
        // Clear dummy script
        rawTx.vin.back().scriptSig = CScript();
    }

    UniValue signparams(UniValue::VARR);
    signparams.push_back(EncodeHexTx(rawTx));
    UniValue signedTxObj = signrawtransaction(signparams, false);

    UniValue sendparams(UniValue::VARR);
    sendparams.push_back(signedTxObj["hex"]);
    return sendrawtransaction(sendparams, false);
}


// Here (but not a class method) just by similarity with other '..ToJSON'
UniValue mnToJSON(CMasternode const & node)
{
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("name", node.name));
    ret.push_back(Pair("ownerAuthAddress", EncodeDestination(node.ownerAuthAddress)));
    ret.push_back(Pair("operatorAuthAddress", EncodeDestination(node.operatorAuthAddress)));

    UniValue ownerRewardAddressJSON(UniValue::VOBJ);
    ScriptPubKeyToJSON(node.ownerRewardAddress, ownerRewardAddressJSON, true);
    ret.push_back(Pair("ownerRewardAddress", ownerRewardAddressJSON));

    UniValue operatorRewardAddressJSON(UniValue::VOBJ);
    ScriptPubKeyToJSON(node.operatorRewardAddress, operatorRewardAddressJSON, true);
    ret.push_back(Pair("operatorRewardAddress", operatorRewardAddressJSON));
    ret.push_back(Pair("operatorRewardRatio", ValueFromAmount(node.operatorRewardRatio * COIN / MN_BASERATIO)));

    ret.push_back(Pair("height", static_cast<uint64_t>(node.height)));
    ret.push_back(Pair("minActivationHeight", static_cast<uint64_t>(node.minActivationHeight)));
    ret.push_back(Pair("activationHeight", static_cast<int>(node.activationHeight)));
    ret.push_back(Pair("deadSinceHeight", static_cast<int>(node.deadSinceHeight)));

    ret.push_back(Pair("activationTx", node.activationTx.GetHex()));
    ret.push_back(Pair("collateralSpentTx", node.collateralSpentTx.GetHex()));
    ret.push_back(Pair("dismissFinalizedTx", node.dismissFinalizedTx.GetHex()));

    ret.push_back(Pair("counterVotesFrom", static_cast<uint64_t>(node.counterVotesFrom)));
    ret.push_back(Pair("counterVotesAgainst", static_cast<uint64_t>(node.counterVotesAgainst)));

    return ret;
}

UniValue dumpnode(uint256 const & id, CMasternode const & node, bool verbose)
{
    UniValue entry(UniValue::VOBJ);
    entry.push_back(Pair("id", id.GetHex()));
    entry.push_back(Pair("status", node.GetHumanReadableStatus()));
    if (verbose)
    {
        entry.push_back(Pair("mn", mnToJSON(node)));
    }
    return entry;
}

UniValue mn_list(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw std::runtime_error("@todo: Help"
    );
    EnsureSaplingUpgrade();

    LOCK(cs_main);

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VBOOL), true);

    UniValue inputs(UniValue::VARR);
    if (params.size() > 0)
    {
        inputs = params[0].get_array();
    }
    bool verbose = false;
    if (params.size() > 1)
    {
        verbose = params[1].get_bool();
    }

    UniValue ret(UniValue::VARR);
    CMasternodes const & mns = pmasternodesview->GetMasternodes();
    if (inputs.empty())
    {
        // Dumps all!
        for (auto it = mns.begin(); it != mns.end(); ++it)
        {
            ret.push_back(dumpnode(it->first, it->second, verbose));
        }
    }
    else
    {
        for (size_t idx = 0; idx < inputs.size(); ++idx)
        {
            uint256 id = ParseHashV(inputs[idx], "masternode id");
            auto const & node = pmasternodesview->ExistMasternode(id);
            if (node)
            {
                ret.push_back(dumpnode(id, *node, verbose));
            }
        }
    }
    return ret;
}

UniValue mn_listactive(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw std::runtime_error("@todo: Help"
    );
    EnsureSaplingUpgrade();

    LOCK(cs_main);

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VBOOL), true);

    UniValue inputs(UniValue::VARR);
    if (params.size() > 0)
    {
        inputs = params[0].get_array();
    }
    bool verbose = false;
    if (params.size() > 1)
    {
        verbose = params[1].get_bool();
    }

    UniValue ret(UniValue::VARR);
    CActiveMasternodes const & mns = pmasternodesview->GetActiveMasternodes();
    if (inputs.empty())
    {
        // Dumps all!
        for (auto id : mns)
        {
            ret.push_back(dumpnode(id, *pmasternodesview->ExistMasternode(id), verbose));
        }
    }
    else
    {
        for (size_t idx = 0; idx < inputs.size(); ++idx)
        {
            uint256 id = ParseHashV(inputs[idx], "masternode id");
            auto const & node = pmasternodesview->ExistMasternode(id);
            if (node && node->IsActive())
            {
                ret.push_back(dumpnode(id, *node, verbose));
            }
        }
    }
    return ret;
}


UniValue dumpvote(uint256 const & voteid, CDismissVote const & vote)
{
    UniValue entry(UniValue::VOBJ);
    entry.push_back(Pair("id", voteid.GetHex()));
    entry.push_back(Pair("reasonCode", static_cast<int>(vote.reasonCode)));
    entry.push_back(Pair("reasonDesc", vote.reasonDescription));
    return entry;
}


UniValue dumpnodevotes(uint256 const & nodeId, CMasternode const & node)
{
    // 'node' is for counters or smth
    UniValue votesFrom(UniValue::VARR);
    {
        auto const & range = pmasternodesview->GetActiveVotesFrom().equal_range(nodeId);
        std::for_each(range.first, range.second, [&] (std::pair<uint256 const, uint256> const & it)
        {
            // it.first == nodeId (from), it.second == voteId
            CDismissVote const & vote = pmasternodesview->GetVotes().at(it.second);
            votesFrom.push_back(dumpvote(vote.against, vote));
        });
    }
    UniValue votesAgainst(UniValue::VARR);
    {
        auto const & range = pmasternodesview->GetActiveVotesAgainst().equal_range(nodeId);
        std::for_each(range.first, range.second, [&] (std::pair<uint256 const, uint256> const & it)
        {
            // it.first == nodeId (against), it.second == voteId
            CDismissVote const & vote = pmasternodesview->GetVotes().at(it.second);
            votesAgainst.push_back(dumpvote(vote.from, vote));
        });
    }
    UniValue entry(UniValue::VOBJ);
    entry.push_back(Pair("nodeId", nodeId.GetHex()));
    entry.push_back(Pair("from", votesFrom));
    entry.push_back(Pair("against", votesAgainst));
    return entry;
}

UniValue mn_listdismissvotes(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error("@todo: Help"
    );
    EnsureSaplingUpgrade();

    LOCK(cs_main);

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VBOOL), true);

    UniValue inputs(UniValue::VARR);
    if (params.size() > 0)
    {
        inputs = params[0].get_array();
    }

    UniValue ret(UniValue::VARR);
    CMasternodes const & mns = pmasternodesview->GetMasternodes();
    if (inputs.empty())
    {
        // Dumps all!
        for (auto it = mns.begin(); it != mns.end(); ++it)
        {
            ret.push_back(dumpnodevotes(it->first, it->second));
        }
    }
    else
    {
        for (size_t idx = 0; idx < inputs.size(); ++idx)
        {
            uint256 id = ParseHashV(inputs[idx], "masternode id");
            auto const & node = pmasternodesview->ExistMasternode(id);
            if (node)
            {
                ret.push_back(dumpnodevotes(id, *node));
            }
        }
    }
    return ret;
}

static const CRPCCommand commands[] =
{ //  category          name                          actor (function)              okSafeMode
  //  ----------------- ------------------------      -----------------------       ----------
    { "masternodes",    "mn_announce",                &mn_announce,                 true  },
    { "masternodes",    "mn_activate",                &mn_activate,                 true  },
    { "masternodes",    "mn_dismissvote",             &mn_dismissvote,              true  },
    { "masternodes",    "mn_dismissvoterecall",       &mn_dismissvoterecall,        true  },
    { "masternodes",    "mn_finalizedismissvoting",   &mn_finalizedismissvoting,    true  },
    { "masternodes",    "mn_setoperator",             &mn_setoperator,              true  },

    { "masternodes",    "mn_list",                    &mn_list,                     true  },
    { "masternodes",    "mn_listactive",              &mn_listactive,               true  },
    { "masternodes",    "mn_listdismissvotes",        &mn_listdismissvotes,         true  },

    /* Not shown in help */
    { "hidden",         "mn_resign",                  &mn_resign,                   true  },
};

void RegisterMasternodesRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
