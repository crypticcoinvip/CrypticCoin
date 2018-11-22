// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key_io.h"
#include "main.h"
#include "crypto/equihash.h"

#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

#include "base58.h"

#include "chainparamsseeds.h"

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, const uint256& nNonce, const std::vector<unsigned char>& nSolution, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    // To create a genesis block for a new chain which is Overwintered:
    //   txNew.nVersion = OVERWINTER_TX_VERSION
    //   txNew.fOverwintered = true
    //   txNew.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID
    //   txNew.nExpiryHeight = <default value>
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 520617983 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nSolution = nSolution;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(txNew);
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = genesis.BuildMerkleTree();
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database (and is in any case of zero value).
 *
 * >>> from pyblake2 import blake2s
 * >>> 'Crypticcoin' + blake2s(b'Security, Anonymity, Privacy. All in one! CrypticCoin. BTC-wallet: 161K6S7WkyVE4UUGrqA1EQhntPXPikdazr').hexdigest()
 *
 * CBlock(hash=00040fe8, ver=4, hashPrevBlock=00000000000000, hashMerkleRoot=c4eaa5, nTime=1533007800, nBits=1f07ffff, nNonce=4695, vtx=1)
 *   CTransaction(hash=c4eaa5, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff071f0104455a6361736830623963346565663862376363343137656535303031653335303039383462366665613335363833613763616331343161303433633432303634383335643334)
 *     CTxOut(nValue=0.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: c4eaa5
 */
static CBlock CreateGenesisBlock(uint32_t nTime, const uint256& nNonce, const std::vector<unsigned char>& nSolution, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "Crypticcoin0f2fa43185a729acc3ddf543dbe80d03bb833aa371090565cc28b965d7ed4030";
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nSolution, nBits, nVersion, genesisReward);
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

const arith_uint256 maxUint = UintToArith256(uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        strCurrencyUnits = "CRYP";
        bip44CoinType = 133; // As registered in https://github.com/satoshilabs/slips/blob/master/slip-0044.md
        consensus.fCoinbaseMustBeProtected = true;
        consensus.nSubsidySlowStartInterval = 20000;
        consensus.nSubsidyHalvingInterval = 10483200;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 4000;
        consensus.powLimit = uint256S("0007ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowAveragingWindow = 17;
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 32; // 32% adjustment down
        consensus.nPowMaxAdjustUp = 16; // 16% adjustment up
        consensus.nPowTargetSpacing = 2.5 * 60;
        consensus.nPowAllowMinDifficultyBlocksAfterHeight = boost::none;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight =
            Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170005;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = 347500;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170007;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight = 100000;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000281b32ff3198a1");

        /**
         * The message start string should be awesome! Ⓒ
         */
        pchMessageStart[0] = 0x24;
        pchMessageStart[1] = 0xe9;
        pchMessageStart[2] = 0x27;
        pchMessageStart[3] = 0x64;
        vAlertPubKey = ParseHex("04b7ecf0baa90495ceb4e4090f6b2fd37eec1e9c85fac68a487f3ce11589692e4a317479316ee814e066638e1db54e37a10689b70286e6315b1087b6615d179264");
        nDefaultTorServicePort = 23303;
        nDefaultPort = 23303;
        nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 100000;
        const size_t N = 200, K = 9;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;

        genesis = CreateGenesisBlock(
            1533007800,
            uint256S("0x000000000000000000000000000000000000000000000000000000000000264e"),
            ParseHex("00324610994396bd15f001091c2c68bde62bd353d207c584a79ecf80e18d5300aa2ea973909012cf50610a216f4c8ac78921802601e1073c959dacf451ff7e1bdc9e30fd5b0f52ffdad4090661e7b62eb0bfa4ac0332656c6c36bbc3eb6c516574f39395ba655ab95f080a308d91072041931bd08f69cbb149d22bf8d9270580db38b8c1c0db70f6d295b9971f69fed0909275300f1b4d6ff118a9b2e1da11967eb26340115d256a04ae2f47591a19db843a331b615306a29b7c3fd05404b966964ba777cb7cf558d093d066f6d4a2d81620070cbbd2498fea39aef642b6d2fe520154a95101f9078b91937488e4724b05b2f54e6f56855c5ad8e82c0b3648de754da592aecba1bf12339c7cfcac7c3be31d343b0e056699076c6443e5597b10125b2afd3bf90d3495e996dfe975e7d44568ed378ccde4de57a64457364e7891a57781be6ebb63b85c1b8f55c63cf68f0104f09d8a8e4d46b667478530d0944e107c3529ac47f1c769e4a78e4da64df80f8cef297f67f13bced316a5127bf39f4f8bcd0a03cb5456cd31ac577a932c3bb91222acd13c44ce3e041b91d54b212d3575c49e0348c60fcb14d5d9e3c3a2a54dfc6654c820de8686185b3ae004d868f4e901034a14a785994a362fff0d06e6b451a9de9f0b59b223d2eae2000961308bca171399c668c508796fcb1a5237bd27605de6ebd17f0a1536641443d370edc74245bbc76e511f18365b057c2043bcf0f3f0a4d9a84bc6deb7d61d021a3b7d7890194995a253d2106d6d41453cf24cddbb4621fb02db2c7cc43d96ef96b9cf91b7d23246fb6b3af8dc171f24369cd5d3a8c61b7faa82b15f3577ad8e6016c3b82b38e22783e88cb9904886a3dab5b709e9379f4b41411d14f0a4dfaf47b72b18bdca7f4f1e39ceddb75f61c9456809acdc35b1f6b6dcb853f64a71531841b2011e526be6870cdc763b02dcde6948b6eb2fb926ec7c25f53b7d21451dfa8ee88b22e6367286165bee4516e1d75697d724edd29f52a9ed69ead0dba93d05481bd057864a36143ffa9da1efd83ad96c969738c5e80d7a70a38193538aaa1846161439e07f3d315fcc3f139edbf4bc9be060e283e3ff78a4085555b65c3fdb4445eed0203498a7a65eb5ecd0ece44dd81773a05149640c4da857492baea334f7caccd75ebe2d7e6d9e04f039acac14312dfe81947654c41ac534fdb165f4134f28bd7bc57f8b09d808ba9cf7a63f63a4de777f184b1a003ba8e8bf6241949d2edbbc4284161f5ff38c29053d71ec37eb6b851c8d8bfcf99f6a4c5fa5b00c5484508a129875be4ca57ed67026eb1ca23b337f107a7d79851d9cf55cd10782f0e6b82efab7bc7b4a130ac7901f7a3887e02684d9d464823635bd371a04159eba627ebc1bbdead0c43df5d3704df54cdb7980022f524a7e6444f979b4539e9da50abd05d437073d1cea02bb548f93c7fa80e4a5d4c5005da60c1a369307560cd973da8e24fe9ba0d2f949829a462d7a4be03f5fda5da82888c778fc840890532d52e0b93a16a90544de86a24f0e195546f20cc221f8834687db6f211bf1ad4f03220b5bde9a1586f77aaa035031fc0d3409281ba709c9c4314bbf20aba16c7b099f117ddf0d71ecd6298d71bce7ddbc99f7e9f935b32b693addb30538f2f88bd1c01f0b9780d5358bf9068a1ada2dde069589dc5f2977b3d853e8925252e703267ddb86bc10764e5fdd44eace8e1de5fdade309aa6618d44b263f498ece1b6b501b902985cac5be445a61a5be59d1059e4b932aa03c75024cc2a660f96a9db958be3c0a8555451f5ff2c507be26c9ff73fada1f3965fa849707b400f2fc49ab2c8c048665d3f5ce45ea28f304ff25654981218bb79cb241a260de2844bca5263043e3"),
            0x1f07ffff, 4, 0);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000095aa3b6953c0757dbd0c6ba828fefab484a15eec5ea6c3d2776e6ea4b38c"));
        assert(genesis.hashMerkleRoot == uint256S("0x1927ef984ff76fead7fb8b0304d0973326fb33289de1fa97e9b3d823e14fd8cb "));

        vFixedSeeds.clear();
        vSeeds.clear();

        // guarantees the first 2 characters, when base58 encoded, are "c1"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x13,0xB6};
        // guarantees the first 2 characters, when base58 encoded, are "c3"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x13,0xBB};
        // the first character, when base58 encoded, is "5" or "K" or "L" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0x80};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x88,0xB2,0x1E};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x88,0xAD,0xE4};
        // guarantees the first 2 characters, when base58 encoded, are "cc"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0xB7,0xA1,0x00};
        // guarantees the first 4 characters, when base58 encoded, are "CCVK"
        base58Prefixes[ZCVIEWING_KEY]      = {0x0D,0x14,0x51,0x40};
        // guarantees the first 4 characters, when base58 encoded, are "CCSK"
        base58Prefixes[ZCSPENDING_KEY]     = {0x03,0xE2,0xA8,0x58};

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "zs";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "zviews";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "zivks";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "secret-extended-key-main";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (0, consensus.hashGenesisBlock)
            (1, uint256S("0x0006af774e069a29f346889a52c737f9b054de1649801241b3ec60e84484ecde"))
            (2590, uint256S("0x000517e541aa6743b39fead56b3254bef4f8119b5cdb249c24e0146f188365eb"))
            (10000, uint256S("0x0000264991d920934cf2d70303c99e203fc00e8a271658a3e6bac68ca922124c"))
            (25000, uint256S("0x0000000977377c91961efa4da9c23688de3172493ba31513b1e1e0aeff122822"))
            (44585, uint256S("0x00000007ddd54c42f44d7b236811793743647e677042a6b332a6c4fb0c6198c5")),
            1541482774,     // * UNIX timestamp of last checkpoint block
            115266,  // * total number of transactions between genesis and last checkpoint
                   //   (the tx=... number in the SetBestChain debug.log lines)
            1.0    // * estimated number of transactions per day after checkpoint
                   //   total number of tx / (checkpoint block height / (24 * 24))
        };

        // Founders reward script expects a vector of 2-of-3 multisig addresses
        vFoundersRewardAddress = {};
        assert(vFoundersRewardAddress.size() <= consensus.GetLastFoundersRewardBlockHeight());
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        strCurrencyUnits = "TAС";
        bip44CoinType = 1;
        consensus.fCoinbaseMustBeProtected = true;
        consensus.nSubsidySlowStartInterval = 20000;
        consensus.nSubsidyHalvingInterval = 10483200;
        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 400;
        consensus.powLimit = uint256S("07ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowAveragingWindow = 17;
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 32; // 32% adjustment down
        consensus.nPowMaxAdjustUp = 16; // 16% adjustment up
        consensus.nPowTargetSpacing = 2.5 * 60;
        consensus.nPowAllowMinDifficultyBlocksAfterHeight = 299187;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight =
            Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170003;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = 207500;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170007;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight = 280000;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000000001d0c4d9cd");

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0x1a;
        pchMessageStart[2] = 0xf9;
        pchMessageStart[3] = 0xbf;
        vAlertPubKey = ParseHex("044e7a1553392325c871c5ace5d6ad73501c66f4c185d6b0453cf45dec5a1322e705c672ac1a27ef7cdaf588c10effdf50ed5f95f85f2f54a5f6159fca394ed0c6");
        nDefaultTorServicePort = 23313;
        nDefaultPort = 23313;
        nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 1000;
        const size_t N = 200, K = 9;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;

        genesis = CreateGenesisBlock(
            1533007800,
            uint256S("0x000000000000000000000000000000000000000000000000000000000000264e"),
            ParseHex("00324610994396bd15f001091c2c68bde62bd353d207c584a79ecf80e18d5300aa2ea973909012cf50610a216f4c8ac78921802601e1073c959dacf451ff7e1bdc9e30fd5b0f52ffdad4090661e7b62eb0bfa4ac0332656c6c36bbc3eb6c516574f39395ba655ab95f080a308d91072041931bd08f69cbb149d22bf8d9270580db38b8c1c0db70f6d295b9971f69fed0909275300f1b4d6ff118a9b2e1da11967eb26340115d256a04ae2f47591a19db843a331b615306a29b7c3fd05404b966964ba777cb7cf558d093d066f6d4a2d81620070cbbd2498fea39aef642b6d2fe520154a95101f9078b91937488e4724b05b2f54e6f56855c5ad8e82c0b3648de754da592aecba1bf12339c7cfcac7c3be31d343b0e056699076c6443e5597b10125b2afd3bf90d3495e996dfe975e7d44568ed378ccde4de57a64457364e7891a57781be6ebb63b85c1b8f55c63cf68f0104f09d8a8e4d46b667478530d0944e107c3529ac47f1c769e4a78e4da64df80f8cef297f67f13bced316a5127bf39f4f8bcd0a03cb5456cd31ac577a932c3bb91222acd13c44ce3e041b91d54b212d3575c49e0348c60fcb14d5d9e3c3a2a54dfc6654c820de8686185b3ae004d868f4e901034a14a785994a362fff0d06e6b451a9de9f0b59b223d2eae2000961308bca171399c668c508796fcb1a5237bd27605de6ebd17f0a1536641443d370edc74245bbc76e511f18365b057c2043bcf0f3f0a4d9a84bc6deb7d61d021a3b7d7890194995a253d2106d6d41453cf24cddbb4621fb02db2c7cc43d96ef96b9cf91b7d23246fb6b3af8dc171f24369cd5d3a8c61b7faa82b15f3577ad8e6016c3b82b38e22783e88cb9904886a3dab5b709e9379f4b41411d14f0a4dfaf47b72b18bdca7f4f1e39ceddb75f61c9456809acdc35b1f6b6dcb853f64a71531841b2011e526be6870cdc763b02dcde6948b6eb2fb926ec7c25f53b7d21451dfa8ee88b22e6367286165bee4516e1d75697d724edd29f52a9ed69ead0dba93d05481bd057864a36143ffa9da1efd83ad96c969738c5e80d7a70a38193538aaa1846161439e07f3d315fcc3f139edbf4bc9be060e283e3ff78a4085555b65c3fdb4445eed0203498a7a65eb5ecd0ece44dd81773a05149640c4da857492baea334f7caccd75ebe2d7e6d9e04f039acac14312dfe81947654c41ac534fdb165f4134f28bd7bc57f8b09d808ba9cf7a63f63a4de777f184b1a003ba8e8bf6241949d2edbbc4284161f5ff38c29053d71ec37eb6b851c8d8bfcf99f6a4c5fa5b00c5484508a129875be4ca57ed67026eb1ca23b337f107a7d79851d9cf55cd10782f0e6b82efab7bc7b4a130ac7901f7a3887e02684d9d464823635bd371a04159eba627ebc1bbdead0c43df5d3704df54cdb7980022f524a7e6444f979b4539e9da50abd05d437073d1cea02bb548f93c7fa80e4a5d4c5005da60c1a369307560cd973da8e24fe9ba0d2f949829a462d7a4be03f5fda5da82888c778fc840890532d52e0b93a16a90544de86a24f0e195546f20cc221f8834687db6f211bf1ad4f03220b5bde9a1586f77aaa035031fc0d3409281ba709c9c4314bbf20aba16c7b099f117ddf0d71ecd6298d71bce7ddbc99f7e9f935b32b693addb30538f2f88bd1c01f0b9780d5358bf9068a1ada2dde069589dc5f2977b3d853e8925252e703267ddb86bc10764e5fdd44eace8e1de5fdade309aa6618d44b263f498ece1b6b501b902985cac5be445a61a5be59d1059e4b932aa03c75024cc2a660f96a9db958be3c0a8555451f5ff2c507be26c9ff73fada1f3965fa849707b400f2fc49ab2c8c048665d3f5ce45ea28f304ff25654981218bb79cb241a260de2844bca5263043e3"),
            0x1f07ffff, 4, 0);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000095aa3b6953c0757dbd0c6ba828fefab484a15eec5ea6c3d2776e6ea4b38c"));
        assert(genesis.hashMerkleRoot == uint256S("0x1927ef984ff76fead7fb8b0304d0973326fb33289de1fa97e9b3d823e14fd8cb"));

        vFixedSeeds.clear();
        vSeeds.clear();

        // guarantees the first 2 characters, when base58 encoded, are "T1"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x0E,0xA4};
        // guarantees the first 2 characters, when base58 encoded, are "T3"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x0E,0xAA};
        // the first character, when base58 encoded, is "9" or "c" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0xEF};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x35,0x87,0xCF};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x35,0x83,0x94};
        // guarantees the first 2 characters, when base58 encoded, are "tc"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x04,0x96,0x90};
        // guarantees the first 4 characters, when base58 encoded, are "TCVK"
        base58Prefixes[ZCVIEWING_KEY]      = {0x1E,0x9A,0x10,0xC6};
        // guarantees the first 4 characters, when base58 encoded, are "TCSK"
        base58Prefixes[ZCSPENDING_KEY]     = {0x09,0x17,0x1F,0xBA};

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "ztestsapling";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "zviewtestsapling";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "zivktestsapling";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "secret-extended-key-test";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (0, consensus.hashGenesisBlock),
            0,
            0,
            0
        };

        // Founders reward script expects a vector of 2-of-3 multisig addresses
        vFoundersRewardAddress = {};
        assert(vFoundersRewardAddress.size() <= consensus.GetLastFoundersRewardBlockHeight());
    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        strCurrencyUnits = "REG";
        bip44CoinType = 1;
        consensus.fCoinbaseMustBeProtected = false;
        consensus.nSubsidySlowStartInterval = 0;
        consensus.nSubsidyHalvingInterval = 150;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.powLimit = uint256S("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
        consensus.nPowAveragingWindow = 17;
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 0; // Turn off adjustment down
        consensus.nPowMaxAdjustUp = 0; // Turn off adjustment up
        consensus.nPowTargetSpacing = 2.5 * 60;
        consensus.nPowAllowMinDifficultyBlocksAfterHeight = 0;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight =
            Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170003;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170006;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        pchMessageStart[0] = 0xaa;
        pchMessageStart[1] = 0xe8;
        pchMessageStart[2] = 0x3f;
        pchMessageStart[3] = 0x5f;
        nDefaultTorServicePort = 18344;
        nDefaultPort = 18344;
        nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 1000;
        const size_t N = 200, K = 9;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;

        genesis = CreateGenesisBlock(
            1533007800,
            uint256S("0x000000000000000000000000000000000000000000000000000000000000264e"),
            ParseHex("00324610994396bd15f001091c2c68bde62bd353d207c584a79ecf80e18d5300aa2ea973909012cf50610a216f4c8ac78921802601e1073c959dacf451ff7e1bdc9e30fd5b0f52ffdad4090661e7b62eb0bfa4ac0332656c6c36bbc3eb6c516574f39395ba655ab95f080a308d91072041931bd08f69cbb149d22bf8d9270580db38b8c1c0db70f6d295b9971f69fed0909275300f1b4d6ff118a9b2e1da11967eb26340115d256a04ae2f47591a19db843a331b615306a29b7c3fd05404b966964ba777cb7cf558d093d066f6d4a2d81620070cbbd2498fea39aef642b6d2fe520154a95101f9078b91937488e4724b05b2f54e6f56855c5ad8e82c0b3648de754da592aecba1bf12339c7cfcac7c3be31d343b0e056699076c6443e5597b10125b2afd3bf90d3495e996dfe975e7d44568ed378ccde4de57a64457364e7891a57781be6ebb63b85c1b8f55c63cf68f0104f09d8a8e4d46b667478530d0944e107c3529ac47f1c769e4a78e4da64df80f8cef297f67f13bced316a5127bf39f4f8bcd0a03cb5456cd31ac577a932c3bb91222acd13c44ce3e041b91d54b212d3575c49e0348c60fcb14d5d9e3c3a2a54dfc6654c820de8686185b3ae004d868f4e901034a14a785994a362fff0d06e6b451a9de9f0b59b223d2eae2000961308bca171399c668c508796fcb1a5237bd27605de6ebd17f0a1536641443d370edc74245bbc76e511f18365b057c2043bcf0f3f0a4d9a84bc6deb7d61d021a3b7d7890194995a253d2106d6d41453cf24cddbb4621fb02db2c7cc43d96ef96b9cf91b7d23246fb6b3af8dc171f24369cd5d3a8c61b7faa82b15f3577ad8e6016c3b82b38e22783e88cb9904886a3dab5b709e9379f4b41411d14f0a4dfaf47b72b18bdca7f4f1e39ceddb75f61c9456809acdc35b1f6b6dcb853f64a71531841b2011e526be6870cdc763b02dcde6948b6eb2fb926ec7c25f53b7d21451dfa8ee88b22e6367286165bee4516e1d75697d724edd29f52a9ed69ead0dba93d05481bd057864a36143ffa9da1efd83ad96c969738c5e80d7a70a38193538aaa1846161439e07f3d315fcc3f139edbf4bc9be060e283e3ff78a4085555b65c3fdb4445eed0203498a7a65eb5ecd0ece44dd81773a05149640c4da857492baea334f7caccd75ebe2d7e6d9e04f039acac14312dfe81947654c41ac534fdb165f4134f28bd7bc57f8b09d808ba9cf7a63f63a4de777f184b1a003ba8e8bf6241949d2edbbc4284161f5ff38c29053d71ec37eb6b851c8d8bfcf99f6a4c5fa5b00c5484508a129875be4ca57ed67026eb1ca23b337f107a7d79851d9cf55cd10782f0e6b82efab7bc7b4a130ac7901f7a3887e02684d9d464823635bd371a04159eba627ebc1bbdead0c43df5d3704df54cdb7980022f524a7e6444f979b4539e9da50abd05d437073d1cea02bb548f93c7fa80e4a5d4c5005da60c1a369307560cd973da8e24fe9ba0d2f949829a462d7a4be03f5fda5da82888c778fc840890532d52e0b93a16a90544de86a24f0e195546f20cc221f8834687db6f211bf1ad4f03220b5bde9a1586f77aaa035031fc0d3409281ba709c9c4314bbf20aba16c7b099f117ddf0d71ecd6298d71bce7ddbc99f7e9f935b32b693addb30538f2f88bd1c01f0b9780d5358bf9068a1ada2dde069589dc5f2977b3d853e8925252e703267ddb86bc10764e5fdd44eace8e1de5fdade309aa6618d44b263f498ece1b6b501b902985cac5be445a61a5be59d1059e4b932aa03c75024cc2a660f96a9db958be3c0a8555451f5ff2c507be26c9ff73fada1f3965fa849707b400f2fc49ab2c8c048665d3f5ce45ea28f304ff25654981218bb79cb241a260de2844bca5263043e3"),
            0x1f07ffff, 4, 0);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000095aa3b6953c0757dbd0c6ba828fefab484a15eec5ea6c3d2776e6ea4b38c"));
        assert(genesis.hashMerkleRoot == uint256S("0x1927ef984ff76fead7fb8b0304d0973326fb33289de1fa97e9b3d823e14fd8cb"));

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (0, consensus.hashGenesisBlock),
            0,
            0,
            0
        };
        // These prefixes are the same as the testnet prefixes
        base58Prefixes[PUBKEY_ADDRESS]     = {0x1D,0x25};
        base58Prefixes[SCRIPT_ADDRESS]     = {0x1C,0xBA};
        base58Prefixes[SECRET_KEY]         = {0xEF};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x35,0x87,0xCF};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x35,0x83,0x94};
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x16,0xB6};
        base58Prefixes[ZCVIEWING_KEY]      = {0xA8,0xAC,0x0C};
        base58Prefixes[ZCSPENDING_KEY]     = {0xAC,0x08};

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "zregtestsapling";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "zviewregtestsapling";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "zivkregtestsapling";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "secret-extended-key-regtest";

        // Founders reward script expects a vector of 2-of-3 multisig addresses
        vFoundersRewardAddress = {};
        assert(vFoundersRewardAddress.size() <= consensus.GetLastFoundersRewardBlockHeight());
    }

    void UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, int nActivationHeight)
    {
        assert(idx > Consensus::BASE_SPROUT && idx < Consensus::MAX_NETWORK_UPGRADES);
        consensus.vUpgrades[idx].nActivationHeight = nActivationHeight;
    }
};
static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = 0;

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams &Params(CBaseChainParams::Network network) {
    switch (network) {
        case CBaseChainParams::MAIN:
            return mainParams;
        case CBaseChainParams::TESTNET:
            return testNetParams;
        case CBaseChainParams::REGTEST:
            return regTestParams;
        default:
            assert(false && "Unimplemented network");
            return mainParams;
    }
}

void SelectParams(CBaseChainParams::Network network) {
    SelectBaseParams(network);
    pCurrentParams = &Params(network);

    // Some python qa rpc tests need to enforce the coinbase consensus rule
    if (network == CBaseChainParams::REGTEST && mapArgs.count("-regtestprotectcoinbase")) {
        regTestParams.SetRegTestCoinbaseMustBeProtected();
    }
}

bool SelectParamsFromCommandLine()
{
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::MAX_NETWORK_TYPES)
        return false;

    SelectParams(network);
    return true;
}


// Block height must be >0 and <=last founders reward block height
// Index variable i ranges from 0 - (vFoundersRewardAddress.size()-1)
std::string CChainParams::GetFoundersRewardAddressAtHeight(int nHeight) const {
    int maxHeight = consensus.GetLastFoundersRewardBlockHeight();
    assert(nHeight > 0 && nHeight <= maxHeight);

    size_t addressChangeInterval = (maxHeight + vFoundersRewardAddress.size()) / vFoundersRewardAddress.size();
    size_t i = nHeight / addressChangeInterval;
    return vFoundersRewardAddress[i];
}

// Block height must be >0 and <=last founders reward block height
// The founders reward address is expected to be a multisig (P2SH) address
CScript CChainParams::GetFoundersRewardScriptAtHeight(int nHeight) const {
    assert(nHeight > 0 && nHeight <= consensus.GetLastFoundersRewardBlockHeight());

    CTxDestination address = DecodeDestination(GetFoundersRewardAddressAtHeight(nHeight).c_str());
    assert(IsValidDestination(address));
    assert(boost::get<CScriptID>(&address) != nullptr);
    CScriptID scriptID = boost::get<CScriptID>(address); // address is a boost variant
    CScript script = CScript() << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
    return script;
}

std::string CChainParams::GetFoundersRewardAddressAtIndex(int i) const {
    assert(i >= 0 && i < vFoundersRewardAddress.size());
    return vFoundersRewardAddress[i];
}

void UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, int nActivationHeight)
{
    regTestParams.UpdateNetworkUpgradeParameters(idx, nActivationHeight);
}
