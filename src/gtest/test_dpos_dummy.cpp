#include <gtest/gtest.h>

#include "../chain.h"
#include "../chainparams.h"
#include "../random.h"
#include "../main.h"
#include "../consensus/validation.h"
#include "../masternodes/dpos_controller.h"

namespace
{
    CBlockIndex * getChainTip()
    {
        LOCK(cs_main);
        return chainActive.Tip();
    }
}

TEST(dPoS, NoTransactionsMeansEmptyBlock)
{
    CValidationState state{};
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
//    auto consensusParams = Params().GetConsensus();

    EXPECT_EQ(getChainTip(), nullptr);
    ASSERT_DEATH(dpos::getController()->isEnabled(), "nHeight >= 0");
    EXPECT_TRUE(ActivateBestChain(state));
    EXPECT_EQ(getChainTip(), nullptr);
}
