#include <gtest/gtest.h>

#include <mir/analysis/live_variables.hpp>
#include <utils/bitset.hpp>

TEST(Basic, REGtoUID) {
  EXPECT_EQ(1, reg_to_uid(foptim::fmir::uid_to_reg(1)));
  EXPECT_EQ(2, reg_to_uid(foptim::fmir::uid_to_reg(2)));
  EXPECT_EQ(10, reg_to_uid(foptim::fmir::uid_to_reg(10)));
  EXPECT_EQ(50, reg_to_uid(foptim::fmir::uid_to_reg(50)));

  EXPECT_EQ(foptim::fmir::VReg{foptim::fmir::CReg::A},
            foptim::fmir::uid_to_reg(
                reg_to_uid(foptim::fmir::VReg{foptim::fmir::CReg::A})));
  EXPECT_EQ(foptim::fmir::VReg{32},
            foptim::fmir::uid_to_reg(reg_to_uid(foptim::fmir::VReg{32})));
}
