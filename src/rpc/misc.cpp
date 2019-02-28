// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "clientversion.h"
#include "init.h"
#include "key_io.h"
#include "main.h"
#include "net.h"
#include "netbase.h"
#include "rpc/server.h"
#include "timedata.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#endif
#include "../masternodes/heartbeat.h"
#include "../masternodes/dpos_controller.h"

#include <stdint.h>

#include <boost/assign/list_of.hpp>

#include <univalue.h>

#include "crypticcoin/Address.hpp"

using namespace std;

extern CTxDestination GetAccountAddress(std::string strAccount, bool bForceNew=false);

/**
 * @note Do not add or change anything in the information returned by this
 * method. `getinfo` exists for backwards-compatibility only. It combines
 * information from wildly different sources in the program, which is a mess,
 * and is thus planned to be deprecated eventually.
 *
 * Based on the source of the information, new information should be added to:
 * - `getblockchaininfo`,
 * - `getnetworkinfo` or
 * - `getwalletinfo`
 *
 * Or alternatively, create a specific query method for the information.
 **/
UniValue getinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getinfo\n"
            "Returns an object containing various state info.\n"
            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,           (numeric) the server version\n"
            "  \"protocolversion\": xxxxx,   (numeric) the protocol version\n"
            "  \"walletversion\": xxxxx,     (numeric) the wallet version\n"
            "  \"balance\": xxxxxxx,         (numeric) the total Crypticcoin balance of the wallet\n"
            "  \"blocks\": xxxxxx,           (numeric) the current number of blocks processed in the server\n"
            "  \"timeoffset\": xxxxx,        (numeric) the time offset\n"
            "  \"connections\": xxxxx,       (numeric) the number of connections\n"
            "  \"proxy\": \"host:port\",     (string, optional) the proxy used by the server\n"
            "  \"difficulty\": xxxxxx,       (numeric) the current difficulty\n"
            "  \"testnet\": true|false,      (boolean) if the server is using testnet or not\n"
            "  \"keypoololdest\": xxxxxx,    (numeric) the timestamp (seconds since GMT epoch) of the oldest pre-generated key in the key pool\n"
            "  \"keypoolsize\": xxxx,        (numeric) how many new keys are pre-generated\n"
            "  \"unlocked_until\": ttt,      (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
            "  \"paytxfee\": x.xxxx,         (numeric) the transaction fee set in " + CURRENCY_UNIT + "/kB\n"
            "  \"relayfee\": x.xxxx,         (numeric) minimum relay fee for non-free transactions in " + CURRENCY_UNIT + "/kB\n"
            "  \"errors\": \"...\"           (string) any error messages\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getinfo", "")
            + HelpExampleRpc("getinfo", "")
        );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    proxyType proxy;
    GetProxy(NET_IPV4, proxy);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("version", CLIENT_VERSION));
    obj.push_back(Pair("protocolversion", PROTOCOL_VERSION));
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        obj.push_back(Pair("walletversion", pwalletMain->GetVersion()));
        int nMinDepth = 1;
        bool fIncludeWatchonly = false;
        CAmount nBalance = pwalletMain->GetBalance();
        CAmount nCoinbase = pwalletMain->GetCoinbaseBalance();
        CAmount nInstantBalance = pwalletMain->GetInstantBalance();
        CAmount nPrivateBalance = getBalanceZaddr("", nMinDepth, !fIncludeWatchonly);
        CAmount nInstantPrivateBalance = getInstantBalanceZaddr("", !fIncludeWatchonly);
        CAmount nTotalBalance = nBalance + nInstantBalance + nPrivateBalance + nInstantPrivateBalance;
        UniValue balance(UniValue::VOBJ);
        balance.push_back(Pair("transparent", ValueFromAmount(nBalance)));
        balance.push_back(Pair("instant_transparent", ValueFromAmount(nInstantBalance)));
        balance.push_back(Pair("coinbase", ValueFromAmount(nCoinbase)));
        balance.push_back(Pair("private", ValueFromAmount(nPrivateBalance)));
        balance.push_back(Pair("instant_private", ValueFromAmount(nInstantPrivateBalance)));
        balance.push_back(Pair("total", ValueFromAmount(nTotalBalance)));
        obj.push_back(Pair("balance", balance));
    }
#endif
    obj.push_back(Pair("blocks",        (int)chainActive.Height()));
    obj.push_back(Pair("timeoffset",    GetTimeOffset()));
    obj.push_back(Pair("connections",   (int)vNodes.size()));
    obj.push_back(Pair("proxy",         (proxy.IsValid() ? proxy.proxy.ToStringIPPort() : string())));
    obj.push_back(Pair("difficulty",    (double)GetDifficulty()));
    obj.push_back(Pair("testnet",       Params().TestnetToBeDeprecatedFieldRPC()));
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        obj.push_back(Pair("keypoololdest", pwalletMain->GetOldestKeyPoolTime()));
        obj.push_back(Pair("keypoolsize",   (int)pwalletMain->GetKeyPoolSize()));
    }
    if (pwalletMain && pwalletMain->IsCrypted())
        obj.push_back(Pair("unlocked_until", nWalletUnlockTime));
    obj.push_back(Pair("paytxfee",      ValueFromAmount(payTxFee.GetFeePerK())));
#endif
    obj.push_back(Pair("relayfee",      ValueFromAmount(::minRelayTxFee.GetFeePerK())));
    obj.push_back(Pair("errors",        GetWarnings("statusbar")));
    obj.push_back(Pair("dpos",          dpos::getController()->isEnabled(chainActive.Height())));
    return obj;
}

#ifdef ENABLE_WALLET
class DescribeAddressVisitor : public boost::static_visitor<UniValue>
{
public:
    UniValue operator()(const CNoDestination &dest) const { return UniValue(UniValue::VOBJ); }

    UniValue operator()(const CKeyID &keyID) const {
        UniValue obj(UniValue::VOBJ);
        CPubKey vchPubKey;
        obj.push_back(Pair("isscript", false));
        if (pwalletMain && pwalletMain->GetPubKey(keyID, vchPubKey)) {
            obj.push_back(Pair("pubkey", HexStr(vchPubKey)));
            obj.push_back(Pair("iscompressed", vchPubKey.IsCompressed()));
        }
        return obj;
    }

    UniValue operator()(const CScriptID &scriptID) const {
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        obj.push_back(Pair("isscript", true));
        if (pwalletMain && pwalletMain->GetCScript(scriptID, subscript)) {
            std::vector<CTxDestination> addresses;
            txnouttype whichType;
            int nRequired;
            ExtractDestinations(subscript, whichType, addresses, nRequired);
            obj.push_back(Pair("script", GetTxnOutputType(whichType)));
            obj.push_back(Pair("hex", HexStr(subscript.begin(), subscript.end())));
            UniValue a(UniValue::VARR);
            for (const CTxDestination& addr : addresses) {
                a.push_back(EncodeDestination(addr));
            }
            obj.push_back(Pair("addresses", a));
            if (whichType == TX_MULTISIG)
                obj.push_back(Pair("sigsrequired", nRequired));
        }
        return obj;
    }

    UniValue operator()(const CScript & rawscript) const {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("metadata", HexStr(rawscript)));
        return obj;
    }

};
#endif

UniValue validateaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "validateaddress \"crypticcoinaddress\"\n"
            "\nReturn information about the given Crypticcoin address.\n"
            "\nArguments:\n"
            "1. \"crypticcoinaddress\"     (string, required) The Crypticcoin address to validate\n"
            "\nResult:\n"
            "{\n"
            "  \"isvalid\" : true|false,         (boolean) If the address is valid or not. If not, this is the only property returned.\n"
            "  \"address\" : \"crypticcoinaddress\",   (string) The Crypticcoin address validated\n"
            "  \"scriptPubKey\" : \"hex\",       (string) The hex encoded scriptPubKey generated by the address\n"
            "  \"ismine\" : true|false,          (boolean) If the address is yours or not\n"
            "  \"isscript\" : true|false,        (boolean) If the key is a script\n"
            "  \"pubkey\" : \"publickeyhex\",    (string) The hex value of the raw public key\n"
            "  \"iscompressed\" : true|false,    (boolean) If the address is compressed\n"
            "  \"account\" : \"account\"         (string) DEPRECATED. The account associated with the address, \"\" is the default account\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"")
            + HelpExampleRpc("validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"")
        );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif

    CTxDestination dest = DecodeDestination(params[0].get_str());
    bool isValid = IsValidDestination(dest);

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("isvalid", isValid));
    if (isValid)
    {
        std::string currentAddress = EncodeDestination(dest);
        ret.push_back(Pair("address", currentAddress));

        CScript scriptPubKey = GetScriptForDestination(dest);
        ret.push_back(Pair("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end())));

#ifdef ENABLE_WALLET
        isminetype mine = pwalletMain ? IsMine(*pwalletMain, dest) : ISMINE_NO;
        ret.push_back(Pair("ismine", (mine & ISMINE_SPENDABLE) ? true : false));
        ret.push_back(Pair("iswatchonly", (mine & ISMINE_WATCH_ONLY) ? true: false));
        UniValue detail = boost::apply_visitor(DescribeAddressVisitor(), dest);
        ret.pushKVs(detail);
        if (pwalletMain && pwalletMain->mapAddressBook.count(dest))
            ret.push_back(Pair("account", pwalletMain->mapAddressBook[dest].name));
#endif
    }
    return ret;
}


class DescribePaymentAddressVisitor : public boost::static_visitor<UniValue>
{
public:
    UniValue operator()(const libzcash::InvalidEncoding &zaddr) const { return UniValue(UniValue::VOBJ); }

    UniValue operator()(const libzcash::SproutPaymentAddress &zaddr) const {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("type", "sprout"));
        obj.push_back(Pair("payingkey", zaddr.a_pk.GetHex()));
        obj.push_back(Pair("transmissionkey", zaddr.pk_enc.GetHex()));
#ifdef ENABLE_WALLET
        if (pwalletMain) {
            obj.push_back(Pair("ismine", pwalletMain->HaveSproutSpendingKey(zaddr)));
        }
#endif
        return obj;
    }

    UniValue operator()(const libzcash::SaplingPaymentAddress &zaddr) const {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("type", "sapling"));
        obj.push_back(Pair("diversifier", HexStr(zaddr.d)));
        obj.push_back(Pair("diversifiedtransmissionkey", zaddr.pk_d.GetHex()));
#ifdef ENABLE_WALLET
        if (pwalletMain) {
            libzcash::SaplingIncomingViewingKey ivk;
            libzcash::SaplingFullViewingKey fvk;
            bool isMine = pwalletMain->GetSaplingIncomingViewingKey(zaddr, ivk) &&
                pwalletMain->GetSaplingFullViewingKey(ivk, fvk) &&
                pwalletMain->HaveSaplingSpendingKey(fvk);
            obj.push_back(Pair("ismine", isMine));
        }
#endif
        return obj;
    }
};

UniValue z_validateaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "z_validateaddress \"zaddr\"\n"
            "\nReturn information about the given z address.\n"
            "\nArguments:\n"
            "1. \"zaddr\"     (string, required) The z address to validate\n"
            "\nResult:\n"
            "{\n"
            "  \"isvalid\" : true|false,      (boolean) If the address is valid or not. If not, this is the only property returned.\n"
            "  \"address\" : \"zaddr\",         (string) The z address validated\n"
            "  \"type\" : \"xxxx\",             (string) \"sprout\" or \"sapling\"\n"
            "  \"ismine\" : true|false,       (boolean) If the address is yours or not\n"
            "  \"payingkey\" : \"hex\",         (string) [sprout] The hex value of the paying key, a_pk\n"
            "  \"transmissionkey\" : \"hex\",   (string) [sprout] The hex value of the transmission key, pk_enc\n"
            "  \"diversifier\" : \"hex\",       (string) [sapling] The hex value of the diversifier, d\n"
            "  \"diversifiedtransmissionkey\" : \"hex\", (string) [sapling] The hex value of pk_d\n"

            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("z_validateaddress", "\"zcWsmqT4X2V4jgxbgiCzyrAfRT1vi1F4sn7M5Pkh66izzw8Uk7LBGAH3DtcSMJeUb2pi3W4SQF8LMKkU2cUuVP68yAGcomL\"")
            + HelpExampleRpc("z_validateaddress", "\"zcWsmqT4X2V4jgxbgiCzyrAfRT1vi1F4sn7M5Pkh66izzw8Uk7LBGAH3DtcSMJeUb2pi3W4SQF8LMKkU2cUuVP68yAGcomL\"")
        );


#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain->cs_wallet);
#else
    LOCK(cs_main);
#endif

    string strAddress = params[0].get_str();
    auto address = DecodePaymentAddress(strAddress);
    bool isValid = IsValidPaymentAddress(address);

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("isvalid", isValid));
    if (isValid)
    {
        ret.push_back(Pair("address", strAddress));
        UniValue detail = boost::apply_visitor(DescribePaymentAddressVisitor(), address);
        ret.pushKVs(detail);
    }
    return ret;
}


/**
 * Used by addmultisigaddress / createmultisig:
 */
CScript _createmultisig_redeemScript(const UniValue& params)
{
    int nRequired = params[0].get_int();
    const UniValue& keys = params[1].get_array();

    // Gather public keys
    if (nRequired < 1)
        throw runtime_error("a multisignature address must require at least one key to redeem");
    if ((int)keys.size() < nRequired)
        throw runtime_error(
            strprintf("not enough keys supplied "
                      "(got %u keys, but need at least %d to redeem)", keys.size(), nRequired));
    if (keys.size() > 16)
        throw runtime_error("Number of addresses involved in the multisignature address creation > 16\nReduce the number");
    std::vector<CPubKey> pubkeys;
    pubkeys.resize(keys.size());
    for (unsigned int i = 0; i < keys.size(); i++)
    {
        const std::string& ks = keys[i].get_str();
#ifdef ENABLE_WALLET
        // Case 1: Bitcoin address and we have full public key:
        CTxDestination dest = DecodeDestination(ks);
        if (pwalletMain && IsValidDestination(dest)) {
            const CKeyID *keyID = boost::get<CKeyID>(&dest);
            if (!keyID) {
                throw std::runtime_error(strprintf("%s does not refer to a key", ks));
            }
            CPubKey vchPubKey;
            if (!pwalletMain->GetPubKey(*keyID, vchPubKey)) {
                throw std::runtime_error(strprintf("no full public key for address %s", ks));
            }
            if (!vchPubKey.IsFullyValid())
                throw runtime_error(" Invalid public key: "+ks);
            pubkeys[i] = vchPubKey;
        }

        // Case 2: hex public key
        else
#endif
        if (IsHex(ks))
        {
            CPubKey vchPubKey(ParseHex(ks));
            if (!vchPubKey.IsFullyValid())
                throw runtime_error(" Invalid public key: "+ks);
            pubkeys[i] = vchPubKey;
        }
        else
        {
            throw runtime_error(" Invalid public key: "+ks);
        }
    }
    CScript result = GetScriptForMultisig(nRequired, pubkeys);

    if (result.size() > MAX_SCRIPT_ELEMENT_SIZE)
        throw runtime_error(
                strprintf("redeemScript exceeds size limit: %d > %d", result.size(), MAX_SCRIPT_ELEMENT_SIZE));

    return result;
}

UniValue createmultisig(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 2)
    {
        string msg = "createmultisig nrequired [\"key\",...]\n"
            "\nCreates a multi-signature address with n signature of m keys required.\n"
            "It returns a json object with the address and redeemScript.\n"

            "\nArguments:\n"
            "1. nrequired      (numeric, required) The number of required signatures out of the n keys or addresses.\n"
            "2. \"keys\"       (string, required) A json array of keys which are Crypticcoin addresses or hex-encoded public keys\n"
            "     [\n"
            "       \"key\"    (string) Crypticcoin address or hex-encoded public key\n"
            "       ,...\n"
            "     ]\n"

            "\nResult:\n"
            "{\n"
            "  \"address\":\"multisigaddress\",  (string) The value of the new multisig address.\n"
            "  \"redeemScript\":\"script\"       (string) The string value of the hex-encoded redemption script.\n"
            "}\n"

            "\nExamples:\n"
            "\nCreate a multisig address from 2 addresses\n"
            + HelpExampleCli("createmultisig", "2 \"[\\\"t16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"t171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("createmultisig", "2, \"[\\\"t16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"t171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"")
        ;
        throw runtime_error(msg);
    }

    // Construct using pay-to-script-hash:
    CScript inner = _createmultisig_redeemScript(params);
    CScriptID innerID(inner);

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("address", EncodeDestination(innerID)));
    result.push_back(Pair("redeemScript", HexStr(inner.begin(), inner.end())));

    return result;
}

UniValue verifymessage(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "verifymessage \"crypticcoinaddress\" \"signature\" \"message\"\n"
            "\nVerify a signed message\n"
            "\nArguments:\n"
            "1. \"crypticcoinaddress\"    (string, required) The Crypticcoin address to use for the signature.\n"
            "2. \"signature\"       (string, required) The signature provided by the signer in base 64 encoding (see signmessage).\n"
            "3. \"message\"         (string, required) The message that was signed.\n"
            "\nResult:\n"
            "true|false   (boolean) If the signature is verified or not.\n"
            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n"
            + HelpExampleCli("signmessage", "\"t14oHp2v54vfmdgQ3v3SNuQga8JKHTNi2a1\" \"my message\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifymessage", "\"t14oHp2v54vfmdgQ3v3SNuQga8JKHTNi2a1\" \"signature\" \"my message\"") +
            "\nAs json rpc\n"
            + HelpExampleRpc("verifymessage", "\"t14oHp2v54vfmdgQ3v3SNuQga8JKHTNi2a1\", \"signature\", \"my message\"")
        );

    LOCK(cs_main);

    string strAddress  = params[0].get_str();
    string strSign     = params[1].get_str();
    string strMessage  = params[2].get_str();

    CTxDestination destination = DecodeDestination(strAddress);
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }

    const CKeyID *keyID = boost::get<CKeyID>(&destination);
    if (!keyID) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");
    }

    bool fInvalid = false;
    vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    return (pubkey.GetID() == *keyID);
}

UniValue setmocktime(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "setmocktime timestamp\n"
            "\nSet the local time to given timestamp (-regtest only)\n"
            "\nArguments:\n"
            "1. timestamp  (integer, required) Unix seconds-since-epoch timestamp\n"
            "   Pass 0 to go back to using the system time."
        );

    if (!Params().MineBlocksOnDemand())
        throw runtime_error("setmocktime for regression testing (-regtest mode) only");

    // cs_vNodes is locked and node send/receive times are updated
    // atomically with the time change to prevent peers from being
    // disconnected because we think we haven't communicated with them
    // in a long time.
    LOCK2(cs_main, cs_vNodes);

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));
    SetMockTime(params[0].get_int64());

    uint64_t t = GetTime();
    BOOST_FOREACH(CNode* pnode, vNodes) {
        pnode->nLastSend = pnode->nLastRecv = t;
    }

    return NullUniValue;
}

UniValue p2p_get_tx_votes(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1) {
        throw runtime_error(
                    "p2p_get_tx_votes ([ \"txid\",... ])\n"
                    "\nSends p2p message get_tx_votes to all connected nodes\n"
                    "\nArguments:\n"
                    "1. [\"intersected_txid\", ...] (array, optional) Array of txids of interested transactions."
                    "If empty, all the votes are interested\n"
                    "\nExamples:\n"
                    + HelpExampleCli("p2p_get_tx_votes", "")
                    + HelpExampleRpc("p2p_get_tx_votes", ""));
    }

    std::vector<uint256> intersectedTxs{};
    if (!params.empty()) {
        UniValue txids{params[0].get_array()};
        for (size_t idx{0}; idx < txids.size(); idx++) {
            const UniValue& txid{txids[idx]};
            if (txid.get_str().length() != 64 || !IsHex(txid.get_str())) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid txid ")+txid.get_str());
            }
            intersectedTxs.push_back(uint256S(txid.get_str()));
        }
    }
    LOCK(cs_vNodes);
    for(const auto& node : vNodes) {
        node->PushMessage("get_tx_votes", intersectedTxs);
    }
    return NullUniValue;
}

UniValue p2p_get_round_votes(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0) {
        throw runtime_error(
                    "p2p_get_round_votes\n"
                    "\nSends p2p message get_round_votes to all connected nodes\n"
                    "\nExamples:\n"
                    + HelpExampleCli("p2p_get_round_votes", "")
                    + HelpExampleRpc("p2p_get_round_votes", ""));
    }
    LOCK(cs_vNodes);
    for(const auto& node : vNodes) {
        node->PushMessage("get_tx_votes");
    }
    return NullUniValue;
}


UniValue heartbeat_send_message(const UniValue& params, bool fHelp)
{
    if (pwalletMain == nullptr) {
        if (!fHelp) {
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "The wallet has been disabled");
        }
        return NullUniValue;
    }
    if (fHelp) {
        throw runtime_error(
            "heartbeat_send_message ( \"address\" timestamp )\n"
            "\nSends heartbeat p2p message with provided timestamp value.\n"
            "\nArguments:\n"
            "1. \"address\"  (string, optional, default="") The operator authentication address. If empty then default wallet address will be used.\n"
            "2. timestamp   (numeric, optional, default=0) The UNIX epoch time in ms of the heartbeat message. If 0 then current time will be used.\n"
            "\nResult:\n"
            "{\n"
            "\t\"timestamp\": xxx    (numeric) The UNIX epoch time in ms of the heartbeat message was created\n"
            "\t\"signature\": xxx    (string) The signature of the heartbeat message\n"
            "\t\"hash\": xxx         (string) The hash of the heartbeat message\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("heartbeat_send_message", "\"tmYuhEjp35CA75LV9VPdDe8rNnL6gV2r8p6\" 1548923902519")
            + HelpExampleRpc("heartbeat_send_message", "\"tmYuhEjp35CA75LV9VPdDe8rNnL6gV2r8p6\", 1548923902519")
        );
    }

    const std::string address{params.empty() ? std::string{} : params[0].get_str()};
    const CTxDestination destination{(address.empty() ? GetAccountAddress("") : DecodeDestination(address))};

    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Crypticcoin address");
    }

    std::int64_t timestamp{0};
    if (params.size() > 1) {
        timestamp = params[1].get_int64();
    }
    if (timestamp < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid timestamp value");
    }

    CKey key{};
    LOCK2(cs_main, pwalletMain->cs_wallet);
    if (pwalletMain == nullptr || !pwalletMain->GetKey(boost::get<CKeyID>(destination), key)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Invalid account address key");
    }

    const CHeartBeatMessage message{CHeartBeatTracker::getInstance().postMessage(key, timestamp)};
    if(message.IsNull()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Failed to send heartbeat message (can't create signature)");
    }

    UniValue rv{UniValue::VOBJ};
    rv.push_back(Pair("timestamp", message.GetTimestamp()));
    rv.push_back(Pair("signature", HexStr(message.GetSignature())));
    rv.push_back(Pair("hash", message.GetHash().ToString()));
    return rv;
}

UniValue heartbeat_read_messages(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0) {
        throw runtime_error(
            "heartbeat_read_messages\n"
            "\nReads heartbeat p2p messages.\n"
            "\nResult:\n"
            "[\n"
            "\t{\n"
            "\t\ttimestamp: xxx    (numeric) The UNIX epoch time in ms of the heartbeat message was created\n"
            "\t\t\"signature\": xxx    (string) The signature of the heartbeat message\n"
            "\t\t\"hash\": xxx         (string) The hash of the heartbeat message\n"
            "\t},...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("heartbeat_read_messages", "")
            + HelpExampleRpc("heartbeat_read_messages", "")
        );
    }

    UniValue rv{UniValue::VARR};
    for (const auto& message : CHeartBeatTracker::getInstance().getReceivedMessages()) {
        UniValue msg{UniValue::VOBJ};
        msg.push_back(Pair("timestamp", message.GetTimestamp()));
        msg.push_back(Pair("signature", HexStr(message.GetSignature())));
        msg.push_back(Pair("hash", message.GetHash().ToString()));
        rv.push_back(msg);
    }
    return rv;
}


UniValue heartbeat_filter_masternodes(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1) {
        throw runtime_error(
            "heartbeat_filter_masternodes \"filter_name\"\n"
            "\nFilters masternodes by theirs heartbeat statistics.\n"
            "\nArguments:\n"
            "1. \"filter_name\"  (string, required) The filter name. Can be one of the following values: recently, stale, outdated.\n"
            "\nResult:\n"
            "[\n"
            "\t{\n"
            "\t\t\"name\": xxx    (string) The masternode name\n"
            "\t\t\"owner\": xxx    (string) The masternode owner auth address\n"
            "\t\t\"operator\": xxx    (string) The masternode operator auth address\n"
            "\t},...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("heartbeat_filter_masternodes", "\"outdated\"")
            + HelpExampleRpc("heartbeat_filter_masternodes", "\"outdated\"")
        );
    }

    UniValue rv{UniValue::VARR};
    const std::string filterName{params[0].get_str()};
    CHeartBeatTracker::AgeFilter ageFilter{CHeartBeatTracker::RECENTLY};

    if (filterName == "stale") {
        ageFilter = CHeartBeatTracker::STALE;
    } else if (filterName == "outdated") {
        ageFilter = CHeartBeatTracker::OUTDATED;
    } else if (filterName != "recently") {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid filter_name argument");
    }

    for (const auto& masternode : CHeartBeatTracker::getInstance().filterMasternodes(ageFilter)) {
        UniValue mn{UniValue::VOBJ};
        mn.push_back(Pair("name", masternode.name));
        mn.push_back(Pair("owner", masternode.ownerAuthAddress.ToString()));
        mn.push_back(Pair("operator", masternode.operatorAuthAddress.ToString()));
        rv.push_back(mn);
    }
    return rv;
}

UniValue list_instant_transactions(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0) {
        throw runtime_error(
            "list_instant_transactions\n"
            "\nLists committed instant transactions.\n"
            "\nResult:\n"
            "[\n"
            "\t{\n"
            "\t\t\"hash\": xxx         (string) The hash of the instant transaction\n"
            "\t\tvin: xxx              (numeric) The inputs count\n"
            "\t\tvout: xxx              (numeric) The outputs count\n"
            "\t},...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("list_instant_transactions", "")
            + HelpExampleRpc("list_instant_transactions", "")
        );
    }

    UniValue rv{UniValue::VARR};
    for (const auto& tx : dpos::getController()->listCommittedTxs()) {
        UniValue entry{UniValue::VOBJ};
        entry.push_back(Pair("hash", tx.GetHash().GetHex()));
        entry.push_back(Pair("vin", tx.vin.size()));
        entry.push_back(Pair("vout", tx.vout.size()));
        rv.push_back(entry);
    }
    return rv;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "control",            "getinfo",                &getinfo,                true  }, /* uses wallet if enabled */
    { "util",               "validateaddress",        &validateaddress,        true  }, /* uses wallet if enabled */
    { "util",               "z_validateaddress",      &z_validateaddress,      true  }, /* uses wallet if enabled */
    { "util",               "createmultisig",         &createmultisig,         true  },
    { "util",               "verifymessage",          &verifymessage,          true  },

    /* Not shown in help */
    { "hidden",             "setmocktime",            &setmocktime,            true  },
    { "hidden",             "p2p_get_tx_votes",       &p2p_get_tx_votes,       true  },
    { "hidden",             "p2p_get_round_votes",    &p2p_get_round_votes,    true  },
    /* heartbeat */
    { "hidden",     "heartbeat_send_message",       &heartbeat_send_message,        true  },
    { "hidden",     "heartbeat_read_messages",      &heartbeat_read_messages,       true  },
    { "hidden",     "heartbeat_filter_masternodes", &heartbeat_filter_masternodes,  true  },
    /* dPoS */
    { "hidden",     "list_instant_transactions",    &list_instant_transactions,     true  },
};

void RegisterMiscRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
