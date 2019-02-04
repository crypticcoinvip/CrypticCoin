// Copyright (c) 2019 The Crypticcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "../rpc/server.h"

//#include "clientversion.h"
#include "../init.h"
#include "../key_io.h"
#include "../main.h"
#include "masternodes.h"
//#include "net.h"
//#include "netbase.h"
//#include "protocol.h"
#include "../rpc/server.h"
//#include "sync.h"
//#include "timedata.h"
//#include "util.h"
#include "../version.h"
//#include "deprecation.h"
#ifdef ENABLE_WALLET
#include "../wallet/wallet.h"
#endif

#include <stdexcept>
#include <univalue.h>

#include <boost/assign/list_of.hpp>

extern UniValue CallRPC(std::string args);

extern UniValue createrawtransaction(UniValue const & params, bool fHelp); // in rawtransaction.cpp
extern UniValue fundrawtransaction(UniValue const & params, bool fHelp); // in rpcwallet.cpp
extern UniValue signrawtransaction(const UniValue& params, bool fHelp); // in rawtransaction.cpp
extern UniValue sendrawtransaction(const UniValue& params, bool fHelp); // in rawtransaction.cpp
extern UniValue getnewaddress(UniValue const & params, bool fHelp); // in rpcwallet.cpp
extern bool EnsureWalletIsAvailable(bool avoidException);

extern bool DecodeHexTx(CTransaction& tx, const std::string& strHexTx); // in core_io.h
extern std::string EncodeHexTx(const CTransaction& tx);


//{ "masternodes",    "createraw_mn_announce",            &createraw_mn_announce,         true  },
//{ "masternodes",    "createraw_mn_activate",            &createraw_mn_activate,         true  },
//{ "masternodes",    "createraw_mn_dismissvote",         &createraw_mn_dismissvote,      true  },
//{ "masternodes",    "createraw_mn_dismissvoterecall",   &createraw_mn_dismissvoterecall,true  },
//{ "masternodes",    "createraw_mn_finalizevoting",      &createraw_mn_finalizevoting,   true  },

UniValue RawCreateFundSignSend(UniValue params, CKeyID const & changeAddress = CKeyID())
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
    // Pick 'autocreated' change for our changeAddress, if any
    if (changeAddress != CKeyID() && nChangePos != -1)
    {
        tx.vout.back().scriptPubKey = GetScriptForDestination(changeAddress);
    }

// 3. Sign
    UniValue signparams(UniValue::VARR);
    signparams.push_back(EncodeHexTx(tx));
    UniValue signedTxObj = signrawtransaction(signparams, false);
    /* returns {
    "hex": "010000000...",
    "complete": true
    } */

// 4. Send
    UniValue sendparams(UniValue::VARR);
    sendparams.push_back(signedTxObj["hex"]);
    UniValue sent = sendrawtransaction(sendparams, false);
    return sent;
}


/// @todo @mn if inputs not empty, should match first input to given auth
/// if empty - should find coins (at least one UTXO) for given auth
void CheckAuthOfFirstInputOrFund(CKeyID const & auth, UniValue & inputs)
{
    if (inputs.size() > 0)
    {
        // check first input
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
        // partially stolen from 'gettxout'.
        // there is another version with some "pool switch magic" in 'signrawtransaction' and i don't know which is best
        CCoins coins;
        {
            LOCK(mempool.cs);
            CCoinsViewMemPool view(pcoinsTip, mempool);
            CCoins coins;
            if (!view.GetCoins(txid, coins) || !coins.IsAvailable(nOutput))
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Input not found or already spent");
            }
            CScript const & prevPubKey = coins.vout[nOutput].scriptPubKey;
            CTxDestination address;
            ExtractDestination(prevPubKey, address);
            CKeyID const * inputAuth = boost::get<CKeyID>(&address);
            if (!inputAuth || *inputAuth != auth)
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Check of authorization failed");
            }
        }
    }
    else
    {
        // search for just one UTXO matching 'auth'
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
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Check of authorization failed. Can't find any coins matching auth.");
        }
    }
}

UniValue createraw_mn_announce(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw std::runtime_error(
            "createraw_mn_announce [{\"txid\":\"id\",\"vout\":n},...] {\"address\":amount,...}\n"
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
            "      \"name\": name                       (string, required) Masternode human-friendly name, should be at least size 3 and less than 255\n"
            "      \"ownerAuthAddress\": P2PKH          (string, required) Masternode owner auth address (P2PKH only, unique)\n"
            "      \"operatorAuthAddress\": P2PKH       (string, required) Masternode operator auth address (P2PKH only, unique)\n"
            "      \"ownerRewardAddress\": scriptPubKey (string, required) Masternode owner reward address (any valid scriptPubKey)\n"
            "      \"collateralAddress\": scriptPubKey  (string, required) Any valid address for keeping collateral amount (any valid scriptPubKey)\n"
            "    }\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
//            "\nExamples\n"
//            + HelpExampleCli("createraw_mn_announce", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"")
//            + HelpExampleRpc("createraw_mn_announce", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"address\\\":0.01}\"")
        );

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
                    ("ownerAuthAddress",    UniValue::VSTR)
                    ("ownerRewardAddress",  UniValue::VSTR)
                    );
    std::string const name =                      metaObj["name"].getValStr();
    std::string const ownerAuthAddressBase58 =    metaObj["ownerAuthAddress"].getValStr();
    std::string const operatorAuthAddressBase58 = metaObj["operatorAuthAddress"].getValStr();
    std::string const ownerRewardAddress =        metaObj["ownerRewardAddress"].getValStr();
    std::string       collateralAddress =   metaObj["collateralAddress"].getValStr();

    // Parameters validation block
    if (name.size() < 3 || name.size() >= 255)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, 'name' should be at least size 3 and less than 255");
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

    if (collateralAddress.empty())
    {
        collateralAddress = getnewaddress(UniValue(), false).getValStr();
    }
    CTxDestination collateralDest = DecodeDestination(collateralAddress);
    if (collateralDest.which() != 1 && collateralDest.which() != 2)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "collateralAddress does not refer to a PK2H or PK2S address");
    }
    /// @todo @mn deep check of ownerRewardAddress!!!

    CDataStream tmp(MnTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    tmp << static_cast<unsigned char>(MasternodesTxType::AnnounceMasternode)
        << name << *ownerAuthAddress << *operatorAuthAddress << ownerRewardAddress;

    std::vector<unsigned char> metaV(tmp.begin(), tmp.end());
    std::string metaDest = EncodeDestination(CTxDestination(metaV));   // "4DGqvnmXeYuu9nHLDMrZmcr96"

    UniValue vouts(UniValue::VOBJ);
    vouts.push_back(Pair(metaDest, ValueFromAmount(MN_ANNOUNCEMENT_FEE)));
    vouts.push_back(Pair(collateralAddress, ValueFromAmount(MN_COLLATERAL_AMOUNT)));

    UniValue newparams(UniValue::VARR);
    newparams.push_back(params[0]);
    newparams.push_back(vouts);

//    return createrawtransaction(newparams, false);
    return RawCreateFundSignSend(newparams);
}

UniValue createraw_mn_activate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error("@todo: Help"
    );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR), true);
    if (params[0].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 must be non-null");
    }
    auto ids = pmasternodesview->AmIOperator();
    if (!ids)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "You are not an operator!");
    }
    auto optNode = pmasternodesview->ExistMasternode(ids->id);
    assert(optNode); // after AmIOperator it should be true

    CMasternode const & node = *optNode;
    int chainHeight = chainActive.Height() + 1;
    if (node.activationTx != uint256() || node.collateralSpentTx != uint256() || node.dismissFinalizedTx != uint256() || node.minActivationHeight > chainHeight)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string() + "Masternode can't activate cause already active or dismissed or minActivationHeight not reached ");
    }
    UniValue inputs = params[0].get_array();
    CheckAuthOfFirstInputOrFund(node.operatorAuthAddress, inputs);

    CDataStream tmp(MnTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    tmp << static_cast<unsigned char>(MasternodesTxType::ActivateMasternode)
        << ids->id;

    std::vector<unsigned char> metaV(tmp.begin(), tmp.end());
    std::string metaDest = EncodeDestination(CTxDestination(metaV));

    UniValue vouts(UniValue::VOBJ);
    vouts.push_back(Pair(metaDest, ValueFromAmount(MN_ACTIVATION_FEE)));

    UniValue newparams(UniValue::VARR);
    newparams.push_back(inputs);
    newparams.push_back(vouts);

    return RawCreateFundSignSend(newparams, node.operatorAuthAddress);
}

UniValue createraw_mn_dismissvote(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw std::runtime_error("@todo: Help"
    );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ), true);
    if (params[0].isNull() || params[1].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null, and argument 2 expected as object with "
                                                  "{\"against\":hexhash-of-MN-id, \"reason_code\":N, \"reason_desc\":\"description\"}");
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
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("You are reach MAX_DISMISS_VOTES_PER_MN! (%d)", MAX_DISMISS_VOTES_PER_MN));
    }
    if (pmasternodesview->ExistActiveVoteIndex(CMasternodesView::VoteIndex::From, ids->id, against))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Vote against %s already exists!", againstHex));
    }

    UniValue inputs = params[0].get_array();
    CheckAuthOfFirstInputOrFund(node.operatorAuthAddress, inputs);

    CDataStream tmp(MnTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    tmp << static_cast<unsigned char>(MasternodesTxType::DismissVote)
        << against << reason_code << reason_desc;

    std::vector<unsigned char> metaV(tmp.begin(), tmp.end());
    std::string metaDest = EncodeDestination(CTxDestination(metaV));

    UniValue vouts(UniValue::VOBJ);
    vouts.push_back(Pair(metaDest, ValueFromAmount(MN_DISMISSVOTE_FEE)));

    UniValue newparams(UniValue::VARR);
    newparams.push_back(inputs);
    newparams.push_back(vouts);

    return RawCreateFundSignSend(newparams, node.operatorAuthAddress);
}

UniValue createraw_mn_dismissvoterecall(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw std::runtime_error("@todo: Help"
    );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ), true);
    if (params[0].isNull() || params[1].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null, and argument 2 expected as object with "
                                                  "{\"against\":hexhash-of-MN-id}");
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
    CheckAuthOfFirstInputOrFund(ids->operatorAuthAddress, inputs);

    CDataStream tmp(MnTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    tmp << static_cast<unsigned char>(MasternodesTxType::DismissVoteRecall)
        << against;

    std::vector<unsigned char> metaV(tmp.begin(), tmp.end());
    std::string metaDest = EncodeDestination(CTxDestination(metaV));

    UniValue vouts(UniValue::VOBJ);
    vouts.push_back(Pair(metaDest, ValueFromAmount(MN_DISMISSVOTERECALL_FEE)));

    UniValue newparams(UniValue::VARR);
    newparams.push_back(inputs);
    newparams.push_back(vouts);

    return RawCreateFundSignSend(newparams, ids->operatorAuthAddress);
}

UniValue createraw_mn_finalizevoting(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw std::runtime_error("@todo: Help"
    );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ), true);
    if (params[0].isNull() || params[1].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null, and argument 2 expected as object with "
                                                  "{\"against\":hexhash-of-MN-id}");
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
    // no need to check authorization nor special funding
//    CheckAuthOfFirstInputOrFund(node.operatorAuthAddress, inputs);

    CDataStream tmp(MnTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    tmp << static_cast<unsigned char>(MasternodesTxType::FinalizeDismissVoting)
        << against;

    std::vector<unsigned char> metaV(tmp.begin(), tmp.end());
    std::string metaDest = EncodeDestination(CTxDestination(metaV));

    UniValue vouts(UniValue::VOBJ);
    vouts.push_back(Pair(metaDest, ValueFromAmount(MN_FINALIZEDISMISSVOTING_FEE)));

    UniValue newparams(UniValue::VARR);
    newparams.push_back(inputs);
    newparams.push_back(vouts);

    return RawCreateFundSignSend(newparams);
}

UniValue listmns(const UniValue& params, bool fHelp)
{
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("blabla", CLIENT_VERSION));
    return obj;
}

UniValue listactivemns(const UniValue& params, bool fHelp)
{
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("blabla", CLIENT_VERSION));
    return obj;
}

UniValue getdismissvotes(const UniValue& params, bool fHelp)
{
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("blabla", CLIENT_VERSION));
    return obj;
}

static const CRPCCommand commands[] =
{ //  category          name                                actor (function)                okSafeMode
  //  ----------------- ------------------------            -----------------------         ----------
    { "masternodes",    "createraw_mn_announce",            &createraw_mn_announce,         true  },
    { "masternodes",    "createraw_mn_activate",            &createraw_mn_activate,         true  },
    { "masternodes",    "createraw_mn_dismissvote",         &createraw_mn_dismissvote,      true  },
    { "masternodes",    "createraw_mn_dismissvoterecall",   &createraw_mn_dismissvoterecall,true  },
    { "masternodes",    "createraw_mn_finalizevoting",      &createraw_mn_finalizevoting,   true  },
//    { "masternodes",        "createraw_set_operator_reward",&createraw_set_operator_reward, true  },

    { "masternodes",    "listmns",                          &listmns,                       true  },
    { "masternodes",    "listactivemns",                    &listactivemns,                 true  },
    { "masternodes",    "getdismissvotes",                  &getdismissvotes,               true  },

    /* Not shown in help */
//    { "hidden",         "createraw_mn_resign",              &createraw_mn_resign,           true  },
};

void RegisterMasternodesRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
