#include <gtest/gtest.h>
#include <limits>
#include <utils/APInt.hpp>

using namespace foptim::utils;
using namespace foptim;

TEST(Int128Test, Basic) {
  {
    const Int128 a{3};
    const Int128 b{4};
    EXPECT_EQ(a.low, 3);
    EXPECT_EQ(b.low, 4);
    EXPECT_EQ(b, b);
    const auto c = a + b;
    EXPECT_EQ(c.high, a.high);
    EXPECT_EQ(c.high, b.high);
    EXPECT_EQ(c.high, 0);
    EXPECT_EQ(c.low, (a.low + b.low));
    EXPECT_EQ(c.low, 7);
    EXPECT_GT(c, b);

    const auto d = a - b;
    EXPECT_EQ(d.high, -1);
    EXPECT_EQ(d.high, std::numeric_limits<u64>::max());
  }
  {
    const Int128 a{-3};
    const Int128 b{-4};
    EXPECT_EQ(a.low, -3);
    EXPECT_EQ(a.high, -1);
    EXPECT_EQ(b.low, -4);
    EXPECT_EQ(b, b);
    const auto c = a + b;
    EXPECT_EQ(c.high, a.high);
    EXPECT_EQ(c.high, b.high);
    EXPECT_EQ(c.high, -1);
    EXPECT_EQ(c.low, (a.low + b.low));
    EXPECT_EQ(c.low, -7);
    EXPECT_LT(c, a);
    EXPECT_LT(c, b);

    const auto d = a - b;
    EXPECT_EQ(d.high, 0);
    EXPECT_EQ(d.low, 1);
  }
}
