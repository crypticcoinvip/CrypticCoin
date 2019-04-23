#include "../chainparams.h"
#include "../dbwrapper.h"
#include "../masternodes/masternodes.h"
#include "../txdb.h"
#include "../utilstrencodings.h"

#include <gtest/gtest.h>


namespace
{

class FakeMasternodesViewDB : public CMasternodesViewDB
{
public:
    FakeMasternodesViewDB() : CMasternodesViewDB() {}
    ~FakeMasternodesViewDB() override {}

    CTeam const & ReadDposTeam(int blockHeight) const override
    {
        return teams.at(blockHeight);
    }
    void WriteDposTeam(int blockHeight, CTeam const & team) override
    {
        teams[blockHeight] = team;
    }
private:
    std::map<int, CTeam> teams;
};


}

TEST(mn, CalcNextDposTeam_FullV1)
{
    FakeMasternodesViewDB view;

    SelectParams(CBaseChainParams::REGTEST); // teamsize == 4

    CMasternodes mns;
    mns.emplace(uint256S("a"), CMasternode());
    mns.emplace(uint256S("b"), CMasternode());
    mns.emplace(uint256S("c"), CMasternode());
    mns.emplace(uint256S("d"), CMasternode());
    mns.emplace(uint256S("e"), CMasternode());
    mns.emplace(uint256S("f"), CMasternode());

    CActiveMasternodes amns;
    for (auto it = mns.begin(); it != mns.end(); ++it)
    {
        amns.emplace(it->first);
    }

    CTeam newteam;
    {
        CTeam team0;
        team0.emplace(uint256S("a"), TeamData {1, CKeyID()});
        team0.emplace(uint256S("b"), TeamData {2, CKeyID()});
        team0.emplace(uint256S("c"), TeamData {3, CKeyID()});
        team0.emplace(uint256S("d"), TeamData {4, CKeyID()});
        view.WriteDposTeam(10, team0);
    }
    for (int h = 10; h < 50; ++h)
    {
        newteam = view.CalcNextDposTeam(amns, mns, uint256S("1"), h);
    }
    ASSERT_EQ(newteam.size(), 4);
    ASSERT_TRUE(newteam[uint256S("a")].joinHeight == 1);
    ASSERT_TRUE(newteam[uint256S("b")].joinHeight == 2);
    ASSERT_TRUE(newteam[uint256S("c")].joinHeight == 3);

    {
        CTeam team0;
        team0.emplace(uint256S("f"), TeamData {1, CKeyID(uint160(ParseHex("0000000000000000000000000000000000000004")))});
        team0.emplace(uint256S("e"), TeamData {2, CKeyID(uint160(ParseHex("0000000000000000000000000000000000000003")))});
        team0.emplace(uint256S("d"), TeamData {3, CKeyID(uint160(ParseHex("0000000000000000000000000000000000000002")))});
        team0.emplace(uint256S("c"), TeamData {4, CKeyID(uint160(ParseHex("0000000000000000000000000000000000000001")))});
        view.WriteDposTeam(10, team0);
    }
    for (int h = 10; h < 50; ++h)
    {
        newteam = view.CalcNextDposTeam(amns, mns, uint256S("1"), h);
    }
    ASSERT_EQ(newteam.size(), 4);
    ASSERT_TRUE(newteam[uint256S("f")].joinHeight == 1);
    ASSERT_TRUE(newteam[uint256S("e")].joinHeight == 2);
    ASSERT_TRUE(newteam[uint256S("d")].joinHeight == 3);

    {
        CTeam team0;
        team0.emplace(uint256S("f"), TeamData {1, CKeyID(uint160(ParseHex("0000000000000000000000000000000000000001")))});
        team0.emplace(uint256S("e"), TeamData {2, CKeyID(uint160(ParseHex("0000000000000000000000000000000000000002")))});
        team0.emplace(uint256S("d"), TeamData {3, CKeyID(uint160(ParseHex("0000000000000000000000000000000000000003")))});
        team0.emplace(uint256S("c"), TeamData {4, CKeyID(uint160(ParseHex("0000000000000000000000000000000000000004")))});
        view.WriteDposTeam(10, team0);
    }
    for (int h = 10; h < 50; ++h)
    {
        newteam = view.CalcNextDposTeam(amns, mns, uint256S("1"), h);
    }
    ASSERT_EQ(newteam.size(), 4);
    ASSERT_TRUE(newteam[uint256S("f")].joinHeight == 1);
    ASSERT_TRUE(newteam[uint256S("e")].joinHeight == 2);
    ASSERT_TRUE(newteam[uint256S("d")].joinHeight == 3);
}

TEST(mn, CalcNextDposTeam_FullV2)
{
    FakeMasternodesViewDB view;

    SelectParams(CBaseChainParams::REGTEST); // teamsize == 4
    int forkHeight = Params().GetConsensus().nMasternodesV2ForkHeight;

    CMasternodes mns;
    mns.emplace(uint256S("a"), CMasternode());
    mns.emplace(uint256S("b"), CMasternode());
    mns.emplace(uint256S("c"), CMasternode());
    mns.emplace(uint256S("d"), CMasternode());
    mns.emplace(uint256S("e"), CMasternode());
    mns.emplace(uint256S("f"), CMasternode());

    CActiveMasternodes amns;
    for (auto it = mns.begin(); it != mns.end(); ++it)
    {
        amns.emplace(it->first);
    }

    CTeam newteam;
    {
        CTeam team0;
        team0.emplace(uint256S("a"), TeamData {1, CKeyID()});
        team0.emplace(uint256S("b"), TeamData {2, CKeyID()});
        team0.emplace(uint256S("c"), TeamData {3, CKeyID()});
        team0.emplace(uint256S("d"), TeamData {4, CKeyID()});
        view.WriteDposTeam(forkHeight, team0);
    }
    newteam = view.CalcNextDposTeam(amns, mns, uint256S("1"), forkHeight);
    newteam = view.CalcNextDposTeam(amns, mns, uint256S("1"), forkHeight+1);
    newteam = view.CalcNextDposTeam(amns, mns, uint256S("1"), forkHeight+2);

    ASSERT_EQ(newteam.size(), 4);
    // after 3 steps the 4th is still here:
    ASSERT_TRUE(newteam[uint256S("d")].joinHeight == 4);
    newteam = view.CalcNextDposTeam(amns, mns, uint256S("1"), forkHeight+3);
    // and now all were renewed
    for (auto it = newteam.begin(); it != newteam.end(); ++it)
    {
        ASSERT_TRUE(it->second.joinHeight >= forkHeight);
    }

    // ensure now, that team updates every round
    for (int h = 1; h <= 50; ++h)
    {
        newteam = view.CalcNextDposTeam(amns, mns, uint256S("1"), forkHeight+3+h);
        for (auto it = newteam.begin(); it != newteam.end(); ++it)
        {
            ASSERT_TRUE(it->second.joinHeight >= forkHeight + 3 + h - newteam.size() + 1);
        }
    }
}

TEST(mn, CalcNextDposTeam_ResignedV2)
{
    FakeMasternodesViewDB view;

    SelectParams(CBaseChainParams::REGTEST); // teamsize == 4
    int forkHeight = Params().GetConsensus().nMasternodesV2ForkHeight;

    CMasternodes mns;
    mns.emplace(uint256S("a"), CMasternode());
    mns.emplace(uint256S("b"), CMasternode());
    mns.emplace(uint256S("c"), CMasternode());
    mns.emplace(uint256S("d"), CMasternode());
    mns.emplace(uint256S("e"), CMasternode());
    mns.emplace(uint256S("f"), CMasternode());

    CActiveMasternodes amns;
    amns.emplace(uint256S("e"));
    amns.emplace(uint256S("f"));

    CTeam newteam;
    {
        CTeam team0;
        team0.emplace(uint256S("a"), TeamData {1, CKeyID()});
        team0.emplace(uint256S("b"), TeamData {2, CKeyID()});
        team0.emplace(uint256S("c"), TeamData {3, CKeyID()});
        team0.emplace(uint256S("d"), TeamData {4, CKeyID()});
        view.WriteDposTeam(forkHeight, team0);
    }
    newteam = view.CalcNextDposTeam(amns, mns, uint256S("1"), forkHeight);

    ASSERT_EQ(newteam.size(), 2);
    ASSERT_TRUE(newteam[uint256S("e")].joinHeight == forkHeight);
    ASSERT_TRUE(newteam[uint256S("f")].joinHeight == forkHeight);
}

