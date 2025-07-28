#include <gtest/gtest.h>

#include <mir/analysis/live_variables.hpp>
#include <utils/bitset.hpp>

using namespace foptim::utils;
using namespace foptim::fmir;
using namespace foptim;

TEST(Basic, REG_UID) {
  EXPECT_EQ(1, reg_to_uid(uid_to_reg(1)));
  EXPECT_EQ(2, reg_to_uid(uid_to_reg(2)));
  EXPECT_EQ(10, reg_to_uid(uid_to_reg(10)));
  EXPECT_EQ(50, reg_to_uid(uid_to_reg(50)));

  EXPECT_EQ(VReg{CReg::A}, uid_to_reg(reg_to_uid(VReg{CReg::A})));
  EXPECT_EQ(VReg{32}, uid_to_reg(reg_to_uid(VReg{32})));
}
