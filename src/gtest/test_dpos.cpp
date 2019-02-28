#include <gtest/gtest.h>
#include "../masternodes/dpos_controller.h"

TEST(dPoS, getController)
{
    EXPECT_TRUE(dpos::getController() != nullptr);
}
