// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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
    //   txNew.nVersion = 3
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
 * >>> 'Zcash' + blake2s(b'The Economist 2016-10-29 Known unknown: Another crypto-currency is born. BTC#436254 0000000000000000044f321997f336d2908cf8c8d6893e88dbf067e2d949487d ETH#2521903 483039a6b6bd8bd05f0584f9a078d075e454925eb71c1f13eaff59b405a721bb DJIA close on 27 Oct 2016: 18,169.68').hexdigest()
 *
 * CBlock(hash=00040fe8, ver=4, hashPrevBlock=00000000000000, hashMerkleRoot=c4eaa5, nTime=1477641360, nBits=1f07ffff, nNonce=4695, vtx=1)
 *   CTransaction(hash=c4eaa5, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff071f0104455a6361736830623963346565663862376363343137656535303031653335303039383462366665613335363833613763616331343161303433633432303634383335643334)
 *     CTxOut(nValue=0.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: c4eaa5
 */
static CBlock CreateGenesisBlock(uint32_t nTime, const uint256& nNonce, const std::vector<unsigned char>& nSolution, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "Zcash0b9c4eef8b7cc417ee5001e3500984b6fea35683a7cac141a043c42064835d34";
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
        consensus.fCoinbaseMustBeProtected = true;
        consensus.nSubsidySlowStartInterval = 20000;
        consensus.nSubsidyHalvingInterval = 1048320;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 4000;
        consensus.powLimit = uint256S("0007ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowAveragingWindow = 17;
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 32; // 32% adjustment down
        consensus.nPowMaxAdjustUp = 16; // 16% adjustment up
        consensus.nPowTargetSpacing = 2.5 * 60;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight =
            Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170005;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = 347500;

        /**
         * The message start string should be awesome! ⓩ❤
         */
        pchMessageStart[0] = 0x24;
        pchMessageStart[1] = 0xe9;
        pchMessageStart[2] = 0x27;
        pchMessageStart[3] = 0x64;
        vAlertPubKey = ParseHex("04b7ecf0baa90495ceb4e4090f6b2fd37eec1e9c85fac68a487f3ce11589692e4a317479316ee814e066638e1db54e37a10689b70286e6315b1087b6615d179264");
        nDefaultPort = 23303;
        nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 100000;
        const size_t N = 200, K = 9;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;

        genesis = CreateGenesisBlock(
            1530694916,
            uint256S("0x000000000000000000000000000000000000000000000000000000000000001f"),
            ParseHex("01741a231cc275c0d533f3bdbc3f067b58471ef7e60b509871a6ada499d84343ed83236dd14d5cf0b3740f9237b30f875ded5641d375674336c9961fbed9e5241d222d38d2beb76ca212c4cdf78294e3dbbc6f0c073fa83b8e45f85444da3186dd2014d8b144954c7a1828124edaa1a7d949524930cadb40c69e0e1b4e221590f761a91294ad001291c5afd2d4563a435394a92dd715b88cdbc3eae90d86006cc2e2299d8c38817407c8d301674a95ed760c92c0df97cac128294c539f28fb8738c99810e1c06512dd2d231311bed99df8bc0eb7d1ad063ccf9deb348269ca3b56a4e3873ea70213865d65dfcbbe53b4f7d37a34b0e6b72c021a28c30ad9865f7fcab3f7c06fc40ea5d903899f0b4e2b8e1223ac6c8962e04b9e1cd3a10fd83c3da7757140952528f258720c53ecbf398587e3ed01a3da083f82e152eb92d0ab984fe9eacb55363659fa89a1bdb988bb04a58e22671c56757228e2690798b9d644925e31a30f368db8b6729b43b5b5a5b7a35a1865a7ab5c142419ad4a3783c970c3fd26138e7929466dc51e3cd6102e91941ad6a707cd39753774a073952b100879ff710b17ba44460f88f0ba89c0c75bfca1dd182371cfe843a3d4fe9813ec27f8f1651c78c329be222fb39d6c2117e327aca5439ffb25743a04be0d965a015551f0229e913bac5866e7de75e2479ff9c1ce845cb7080110d123f626188853bc6905d768f29fd98173b2640c76447637fb29b621d23c37f54578e34f01f9d8322813d33b7cffd247e4c921462256daed0b33e91e471a222fcbde8d90258deda1562db2e47f7ac06a7e7aa41453cea17aacbb0d84fef54808c95a86b0eddd22fd7739af80b061d36752f867a33edcfa4607b2f4202617e9a2f29927fcc3b2bf9415204c6da1abcdd1b13926cf420e3663ea0fee9b42dfe964158b98fa7f341702d507cd115b9adf2dde10413c6440c16e4ff6ae6a4d35f66aae935cf12955b91e256dbff272a2f980750381cbca076aadbfe23ea09f96a33e1a8ed1de7c311d669debc8162231d006dc2012e322ab9fd3bf5910073b1e8d669d3b05878b0a346568dc37391e3f00b20e7a450e2a46fb56c8b2e2d067f02f3a6267b707aa1347111d84a71b75e86801362e759c55ab255831bf13df05639b0b0f0db97f329a8d4a846dcc29127a2b0d33b7e96b517344a32342e8ce551218ca529bacac28707d95fc38e0cfd9f9e6051c5e52830d329f16f522bd8a5d17a0f541e7c004d46d5acf4b01ac3e695f2fea4fcfebf14c1d9896a642634b772e33a65b87170e7c9a2d83d431d7f56895e166eab5c3a8617eb31d46a86c2e673564e9ff2138ea7ecd629ec2c3367b2812d9e4856e9281749c0bd6c2eb7b37cf2ffffd546f2052b76fc7251e974df052e1e7d1fbd2c9acd6a6fe0379aa36d2de423da9e0621ef45067217dbb31885a15542e7dc2697e895144754f55d8ae5b08b1fd6d432401b372104fd053460bc5e636b8981737aabc42162ba1a4dda76bae79c8c0762d54fbb92f5776ded65203cbbf37d9d00cb980ffd1cbdce5002d64f03d873e4561074b2c383e29d5b3582af0ebf5974c7c5b6497193f3c38657392b1dc4e346a566fda3966a651d5161f56b114b40f34893e8cb696fb3d813a71273691830799429351cb949bb50de1ff2b6ddc713fea9a088510c3fb1c175fa95bda83874595db620b10addda0611f215c729260e1973703e6ad52cf336deabb791b3d435f2e7fa1d4c0a4f8bf34ef1fe54c95edaad3352c0af7a739fc14692b62a741f6aecbfe8f34315c2038202003180658a9194386124acaaf2a5aab4ab584ef18493cdca123eb25d89dd9907be33972a2d47b429045b3dcf5efdc4907dcafde5ef4f4a00baf5b5dea78"),
            0x1f07ffff, 4, 0);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000231e07eb031c630c4734e69ebe29b1f8611488331fdb5f6a2ddb4f12a0a98"));
        assert(genesis.hashMerkleRoot == uint256S("0xc4eaa58879081de3c24a7b117ed2b28300e7ec4c4c1dff1d3f1268b7857a4ddb"));

        vFixedSeeds.clear();
        vSeeds.clear();
//        vSeeds.push_back(CDNSSeedData("z.cash", "dnsseed.z.cash")); // Zcash
//        vSeeds.push_back(CDNSSeedData("str4d.xyz", "dnsseed.str4d.xyz")); // @str4d
//        vSeeds.push_back(CDNSSeedData("znodes.org", "dnsseed.znodes.org")); // @bitcartel

        // guarantees the first 2 characters, when base58 encoded, are "t1"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x1C,0xB8};
        // guarantees the first 2 characters, when base58 encoded, are "t3"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x1C,0xBD};
        // the first character, when base58 encoded, is "5" or "K" or "L" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0x80};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x88,0xB2,0x1E};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x88,0xAD,0xE4};
        // guarantees the first 2 characters, when base58 encoded, are "zc"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x16,0x9A};
        // guarantees the first 4 characters, when base58 encoded, are "ZiVK"
        base58Prefixes[ZCVIEWING_KEY]      = {0xA8,0xAB,0xD3};
        // guarantees the first 2 characters, when base58 encoded, are "SK"
        base58Prefixes[ZCSPENDING_KEY]     = {0xAB,0x36};

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (0, consensus.hashGenesisBlock),
            0,
            0,
            0
        };

        // Founders reward script expects a vector of 2-of-3 multisig addresses
        vFoundersRewardAddress = {
//           "t3Vz22vK5z2LcKEdg16Yv4FFneEL1zg9ojd", /* main-index: 0*/
//           "t3cL9AucCajm3HXDhb5jBnJK2vapVoXsop3", /* main-index: 1*/
//           "t3fqvkzrrNaMcamkQMwAyHRjfDdM2xQvDTR", /* main-index: 2*/
//           "t3TgZ9ZT2CTSK44AnUPi6qeNaHa2eC7pUyF", /* main-index: 3*/
//           "t3SpkcPQPfuRYHsP5vz3Pv86PgKo5m9KVmx", /* main-index: 4*/
//           "t3Xt4oQMRPagwbpQqkgAViQgtST4VoSWR6S", /* main-index: 5*/
//           "t3ayBkZ4w6kKXynwoHZFUSSgXRKtogTXNgb", /* main-index: 6*/
//           "t3adJBQuaa21u7NxbR8YMzp3km3TbSZ4MGB", /* main-index: 7*/
//           "t3K4aLYagSSBySdrfAGGeUd5H9z5Qvz88t2", /* main-index: 8*/
//           "t3RYnsc5nhEvKiva3ZPhfRSk7eyh1CrA6Rk", /* main-index: 9*/
//           "t3Ut4KUq2ZSMTPNE67pBU5LqYCi2q36KpXQ", /* main-index: 10*/
//           "t3ZnCNAvgu6CSyHm1vWtrx3aiN98dSAGpnD", /* main-index: 11*/
//           "t3fB9cB3eSYim64BS9xfwAHQUKLgQQroBDG", /* main-index: 12*/
//           "t3cwZfKNNj2vXMAHBQeewm6pXhKFdhk18kD", /* main-index: 13*/
//           "t3YcoujXfspWy7rbNUsGKxFEWZqNstGpeG4", /* main-index: 14*/
//           "t3bLvCLigc6rbNrUTS5NwkgyVrZcZumTRa4", /* main-index: 15*/
//           "t3VvHWa7r3oy67YtU4LZKGCWa2J6eGHvShi", /* main-index: 16*/
//           "t3eF9X6X2dSo7MCvTjfZEzwWrVzquxRLNeY", /* main-index: 17*/
//           "t3esCNwwmcyc8i9qQfyTbYhTqmYXZ9AwK3X", /* main-index: 18*/
//           "t3M4jN7hYE2e27yLsuQPPjuVek81WV3VbBj", /* main-index: 19*/
//           "t3gGWxdC67CYNoBbPjNvrrWLAWxPqZLxrVY", /* main-index: 20*/
//           "t3LTWeoxeWPbmdkUD3NWBquk4WkazhFBmvU", /* main-index: 21*/
//           "t3P5KKX97gXYFSaSjJPiruQEX84yF5z3Tjq", /* main-index: 22*/
//           "t3f3T3nCWsEpzmD35VK62JgQfFig74dV8C9", /* main-index: 23*/
//           "t3Rqonuzz7afkF7156ZA4vi4iimRSEn41hj", /* main-index: 24*/
//           "t3fJZ5jYsyxDtvNrWBeoMbvJaQCj4JJgbgX", /* main-index: 25*/
//           "t3Pnbg7XjP7FGPBUuz75H65aczphHgkpoJW", /* main-index: 26*/
//           "t3WeKQDxCijL5X7rwFem1MTL9ZwVJkUFhpF", /* main-index: 27*/
//           "t3Y9FNi26J7UtAUC4moaETLbMo8KS1Be6ME", /* main-index: 28*/
//           "t3aNRLLsL2y8xcjPheZZwFy3Pcv7CsTwBec", /* main-index: 29*/
//           "t3gQDEavk5VzAAHK8TrQu2BWDLxEiF1unBm", /* main-index: 30*/
//           "t3Rbykhx1TUFrgXrmBYrAJe2STxRKFL7G9r", /* main-index: 31*/
//           "t3aaW4aTdP7a8d1VTE1Bod2yhbeggHgMajR", /* main-index: 32*/
//           "t3YEiAa6uEjXwFL2v5ztU1fn3yKgzMQqNyo", /* main-index: 33*/
//           "t3g1yUUwt2PbmDvMDevTCPWUcbDatL2iQGP", /* main-index: 34*/
//           "t3dPWnep6YqGPuY1CecgbeZrY9iUwH8Yd4z", /* main-index: 35*/
//           "t3QRZXHDPh2hwU46iQs2776kRuuWfwFp4dV", /* main-index: 36*/
//           "t3enhACRxi1ZD7e8ePomVGKn7wp7N9fFJ3r", /* main-index: 37*/
//           "t3PkLgT71TnF112nSwBToXsD77yNbx2gJJY", /* main-index: 38*/
//           "t3LQtHUDoe7ZhhvddRv4vnaoNAhCr2f4oFN", /* main-index: 39*/
//           "t3fNcdBUbycvbCtsD2n9q3LuxG7jVPvFB8L", /* main-index: 40*/
//           "t3dKojUU2EMjs28nHV84TvkVEUDu1M1FaEx", /* main-index: 41*/
//           "t3aKH6NiWN1ofGd8c19rZiqgYpkJ3n679ME", /* main-index: 42*/
//           "t3MEXDF9Wsi63KwpPuQdD6by32Mw2bNTbEa", /* main-index: 43*/
//           "t3WDhPfik343yNmPTqtkZAoQZeqA83K7Y3f", /* main-index: 44*/
//           "t3PSn5TbMMAEw7Eu36DYctFezRzpX1hzf3M", /* main-index: 45*/
//           "t3R3Y5vnBLrEn8L6wFjPjBLnxSUQsKnmFpv", /* main-index: 46*/
//           "t3Pcm737EsVkGTbhsu2NekKtJeG92mvYyoN", /* main-index: 47*/
////            "t3PZ9PPcLzgL57XRSG5ND4WNBC9UTFb8DXv", /* main-index: 48*/
////            "t3L1WgcyQ95vtpSgjHfgANHyVYvffJZ9iGb", /* main-index: 49*/
////            "t3JtoXqsv3FuS7SznYCd5pZJGU9di15mdd7", /* main-index: 50*/
////            "t3hLJHrHs3ytDgExxr1mD8DYSrk1TowGV25", /* main-index: 51*/
////            "t3fmYHU2DnVaQgPhDs6TMFVmyC3qbWEWgXN", /* main-index: 52*/
////            "t3T4WmAp6nrLkJ24iPpGeCe1fSWTPv47ASG", /* main-index: 53*/
////            "t3fP6GrDM4QVwdjFhmCxGNbe7jXXXSDQ5dv", /* main-index: 54*/
};
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
        strCurrencyUnits = "TAZ";
        consensus.fCoinbaseMustBeProtected = true;
        consensus.nSubsidySlowStartInterval = 20000;
        consensus.nSubsidyHalvingInterval = 1048320;
        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 400;
        consensus.powLimit = uint256S("07ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowAveragingWindow = 17;
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 32; // 32% adjustment down
        consensus.nPowMaxAdjustUp = 16; // 16% adjustment up
        consensus.nPowTargetSpacing = 2.5 * 60;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight =
            Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170003;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = 207500;

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0x1a;
        pchMessageStart[2] = 0xf9;
        pchMessageStart[3] = 0xbf;
        vAlertPubKey = ParseHex("044e7a1553392325c871c5ace5d6ad73501c66f4c185d6b0453cf45dec5a1322e705c672ac1a27ef7cdaf588c10effdf50ed5f95f85f2f54a5f6159fca394ed0c6");
        nDefaultPort = 23313;
        nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 1000;
        const size_t N = 200, K = 9;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;

        genesis = CreateGenesisBlock(
            1530694916,
            uint256S("0x0000000000000000000000000000000000000000000000000000000000000017"),
            ParseHex("00578751dad1ac29bae077cadb770f41fe7232765e5e8024301a605e53cb257afd84f65cbf1cad99a76d0484d84f3741f376bdba50ee6ce25e928d56badc64161fce0cba459448317604fc55aa8a896e82ce0a74042031a0bb02dbb4790e539e44aa9d23ae5d1d975a10d492ad0fedc861c39181c286463df8cf5a2c811f0ee315cb8c4eeafbf741e739e07c5f3fdc591ef3ed1622fdaf7311c523915861962e3f7e688d012c61cf01a73bfc038337ba928f0782c87c99463feadf5bed0aacadb197e3a6937cfa957f6df47b0e65ab7709f5229f87a17b1d9297717fe6200f3fff3ebef39ae4d52a33cf21ea553203e8299328f0c982d24549faaebb0a3148631b89322fe68d221b087061cd8d7b925f1e1c5365455907aa696d39b545e6611da5604a2c4ce10fa970c773096ce1de14c233faca943ca79b2fcca26961eeb54e670ba97b34c8983a4632764290b2cf20015af502e449207eb07f64c4a7f570f5ec3591bb7e0e03267e37d7c523574cc5e68af4742998821428433188bc7f5cdc05b73d53f3e07fb2af1df3929267e236b852f80face159e517a3fa167e64058e62bd360a04320cd1deeeaaa7c5ebf2f538ea539d1b2549d00e203f1e057e22a1e57f6e64d27e3e9c4592749517f314caa662190b8c4ceafbd7a0e2c3166ad32f3d5dbc2091ce6bcc4aaf85cbd5f7b360d8574f60271d1d0301918234d4a8376f51aff293b92b84c98c72315cdb203432e35a8f4d3b4bb9a217dd7033e14d827a7b9906b93dae02223bd53e2a30a9c46869fdfc5756aa640fcd3fb7e0e01583a80ab1139faa630140232fd7a8066515136215d277cfd925ac273ee5b16d799c0ce0440e45a42e1a994ad829a7d64b5fdb62441cbe591f08adb897441b80fddd3d852fef2dc9b2c8335a8fd73658be7c001b98656beb384a54c5692644c1daea7800f37d27794fed97961c0082574de15cbb0a7ac14d49c83e87a5ea2dcffdf88561f7de3b71995b4fa04507dd70c4a1ce21cd7cfa323b51ccd35136dcbb16a730d505b481d86e412a0804aadac8ae86956e18de6801480386d285fa423a18086247ff43e61db8b5c71b3af46f8474da123f896f848028aef3e2897916afb31787efb4145d9c4da72d1325fee7382d706becdcac1dd7b7009c758779b4c321f2de1afa44fc78b34d64062252916d8961f7ee9481a185ad6aee644b1a212007498f84534ff6ebaa04907ee1bdbfbc290bfa305f16e10cc3951bdaeb44df92c50f79973982c19609ee3877921c93a8ea57fe1b74edb7c140d74c2e9c85ab0886db464e2911875cdba0ef93ca24395598f5b7871313b66093236319f06f016d204505c2ab6f3832bc0a7ddbbc279bb63ee059b412c14e22eab1c39738f2610f1b4e6b9e58bd9e8b79807df65e666d75969b19044178bf290d25cb520d246cba675bbd8a5494f30326ed2e409d53a857e32994ff7ff7d37e4a0c348d9905faf040a1ddbe33d29a11ce0c6f350a1ba1d181041c8d5ae9f947a53bab8cd6ee1bd99449eb617c3a042c8b46f73766a547f3cf736658fead21e3f675ffbe3ce613526b14c9bfdea2a4582c538c0d2ce7cab5b13f4034c4fddd65097661565d28e86b4a29aa1916a75924b60056646ecdbbad47089f71e9a224e799b07209a89b1a1dd3abdf0fd500ff30355ba45901eb362f28985230c5dd8767d905e34ff1713620f9a3db9bac1161f0d71821a24ffa14421f8ba82b9dc0d611aef01e1f47318adafda1005bc4e46be75015d50b158ba30b7b94275dd528473f77d0c8a030a4a96e909977d8363b939324af6879a510a4ff5979b0edc5ca95d07210b9e6321d04c58d7945e2b3a74ad125d34f7528d61ac33c2cbe4db121386c8213801a3a99586cfb5fe1"),
            0x2007ffff, 4, 0);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x003d61c56a1a637626019223b12c5aa65e14d7db7fb6af33930473d28cd3e6a9"));
        assert(genesis.hashMerkleRoot == uint256S("0xc4eaa58879081de3c24a7b117ed2b28300e7ec4c4c1dff1d3f1268b7857a4ddb"));

        vFixedSeeds.clear();
        vSeeds.clear();
//        vSeeds.push_back(CDNSSeedData("z.cash", "dnsseed.testnet.z.cash")); // Zcash // TODO: SS remove zCash testnet seed

        // guarantees the first 2 characters, when base58 encoded, are "tm"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x1D,0x25};
        // guarantees the first 2 characters, when base58 encoded, are "t2"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x1C,0xBA};
        // the first character, when base58 encoded, is "9" or "c" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0xEF};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x35,0x87,0xCF};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x35,0x83,0x94};
        // guarantees the first 2 characters, when base58 encoded, are "zt"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x16,0xB6};
        // guarantees the first 4 characters, when base58 encoded, are "ZiVt"
        base58Prefixes[ZCVIEWING_KEY]      = {0xA8,0xAC,0x0C};
        // guarantees the first 2 characters, when base58 encoded, are "ST"
        base58Prefixes[ZCSPENDING_KEY]     = {0xAC,0x08};

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
        vFoundersRewardAddress = {
//           "t2UNzUUx8mWBCRYPRezvA363EYXyEpHokyi", "t2N9PH9Wk9xjqYg9iin1Ua3aekJqfAtE543", "t2NGQjYMQhFndDHguvUw4wZdNdsssA6K7x2", "t2ENg7hHVqqs9JwU5cgjvSbxnT2a9USNfhy",
//           "t2BkYdVCHzvTJJUTx4yZB8qeegD8QsPx8bo", "t2J8q1xH1EuigJ52MfExyyjYtN3VgvshKDf", "t2Crq9mydTm37kZokC68HzT6yez3t2FBnFj", "t2EaMPUiQ1kthqcP5UEkF42CAFKJqXCkXC9",
//           "t2F9dtQc63JDDyrhnfpzvVYTJcr57MkqA12", "t2LPirmnfYSZc481GgZBa6xUGcoovfytBnC", "t26xfxoSw2UV9Pe5o3C8V4YybQD4SESfxtp", "t2D3k4fNdErd66YxtvXEdft9xuLoKD7CcVo",
//           "t2DWYBkxKNivdmsMiivNJzutaQGqmoRjRnL", "t2C3kFF9iQRxfc4B9zgbWo4dQLLqzqjpuGQ", "t2MnT5tzu9HSKcppRyUNwoTp8MUueuSGNaB", "t2AREsWdoW1F8EQYsScsjkgqobmgrkKeUkK",
//           "t2Vf4wKcJ3ZFtLj4jezUUKkwYR92BLHn5UT", "t2K3fdViH6R5tRuXLphKyoYXyZhyWGghDNY", "t2VEn3KiKyHSGyzd3nDw6ESWtaCQHwuv9WC", "t2F8XouqdNMq6zzEvxQXHV1TjwZRHwRg8gC",
//           "t2BS7Mrbaef3fA4xrmkvDisFVXVrRBnZ6Qj", "t2FuSwoLCdBVPwdZuYoHrEzxAb9qy4qjbnL", "t2SX3U8NtrT6gz5Db1AtQCSGjrpptr8JC6h", "t2V51gZNSoJ5kRL74bf9YTtbZuv8Fcqx2FH",
//           "t2FyTsLjjdm4jeVwir4xzj7FAkUidbr1b4R", "t2EYbGLekmpqHyn8UBF6kqpahrYm7D6N1Le", "t2NQTrStZHtJECNFT3dUBLYA9AErxPCmkka", "t2GSWZZJzoesYxfPTWXkFn5UaxjiYxGBU2a",
//           "t2RpffkzyLRevGM3w9aWdqMX6bd8uuAK3vn", "t2JzjoQqnuXtTGSN7k7yk5keURBGvYofh1d", "t2AEefc72ieTnsXKmgK2bZNckiwvZe3oPNL", "t2NNs3ZGZFsNj2wvmVd8BSwSfvETgiLrD8J",
//           "t2ECCQPVcxUCSSQopdNquguEPE14HsVfcUn", "t2JabDUkG8TaqVKYfqDJ3rqkVdHKp6hwXvG", "t2FGzW5Zdc8Cy98ZKmRygsVGi6oKcmYir9n", "t2DUD8a21FtEFn42oVLp5NGbogY13uyjy9t",
//           "t2UjVSd3zheHPgAkuX8WQW2CiC9xHQ8EvWp", "t2TBUAhELyHUn8i6SXYsXz5Lmy7kDzA1uT5", "t2Tz3uCyhP6eizUWDc3bGH7XUC9GQsEyQNc", "t2NysJSZtLwMLWEJ6MH3BsxRh6h27mNcsSy",
//           "t2KXJVVyyrjVxxSeazbY9ksGyft4qsXUNm9", "t2J9YYtH31cveiLZzjaE4AcuwVho6qjTNzp", "t2QgvW4sP9zaGpPMH1GRzy7cpydmuRfB4AZ", "t2NDTJP9MosKpyFPHJmfjc5pGCvAU58XGa4",
//           "t29pHDBWq7qN4EjwSEHg8wEqYe9pkmVrtRP", "t2Ez9KM8VJLuArcxuEkNRAkhNvidKkzXcjJ", "t2D5y7J5fpXajLbGrMBQkFg2mFN8fo3n8cX", "t2UV2wr1PTaUiybpkV3FdSdGxUJeZdZztyt",
            };
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
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight =
            Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170003;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight =
            Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;

        pchMessageStart[0] = 0xaa;
        pchMessageStart[1] = 0xe8;
        pchMessageStart[2] = 0x3f;
        pchMessageStart[3] = 0x5f;
        nDefaultPort = 18344;
        nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 1000;
        const size_t N = 48, K = 5;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;

        genesis = CreateGenesisBlock(
            1296688602,
            uint256S("0x0000000000000000000000000000000000000000000000000000000000000009"),
            ParseHex("01936b7db1eb4ac39f151b8704642d0a8bda13ec547d54cd5e43ba142fc6d8877cab07b3"),
            0x200f0f0f, 4, 0);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x029f11d80ef9765602235e1bc9727e3eb6ba20839319f761fee920d63401e327"));
        assert(genesis.hashMerkleRoot == uint256S("0xc4eaa58879081de3c24a7b117ed2b28300e7ec4c4c1dff1d3f1268b7857a4ddb"));

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData){
            boost::assign::map_list_of
            ( 0, uint256S("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")),
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

        // Founders reward script expects a vector of 2-of-3 multisig addresses
        vFoundersRewardAddress = { /* "t2FwcEhFdNXuFMv1tcYwaBJtYVtMj8b1uTg" */ };
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

    CBitcoinAddress address(GetFoundersRewardAddressAtHeight(nHeight).c_str());
    assert(address.IsValid());
    assert(address.IsScript());
    CScriptID scriptID = boost::get<CScriptID>(address.Get()); // Get() returns a boost variant
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
