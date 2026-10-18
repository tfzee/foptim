#include <gtest/gtest.h>

#include <random>

#include "mir/const_div.hpp"

namespace {
using namespace foptim;
using namespace foptim::fmir;
using u128 = unsigned __int128;
using i128 = __int128;

u64 wmask(unsigned w) { return w == 64 ? ~u64{0} : ((u64{1} << w) - 1); }

u64 run_udiv(const UDivMagic &m, u64 x, unsigned w) {
  if (!m.add) {
    return static_cast<u64>(((u128{x >> m.pre_shift}) * m.mul) >> m.shift);
  }
  u64 t = static_cast<u64>((u128{x} * m.mul) >> w);
  return ((t + ((x - t) >> 1)) >> (m.shift - 1));
}

i64 run_sdiv(const SDivMagic &m, i64 x) {
  i128 p = static_cast<i128>(x) * static_cast<i128>(m.mul);
  i128 q0 = p >> m.shift;
  return static_cast<i64>(q0 + (q0 < 0 ? 1 : 0));
}

i64 sext(u64 x, unsigned w) {
  return static_cast<i64>(x << (64 - w)) >> (64 - w);
}

void check_udiv(u64 d, unsigned w, u64 x) {
  auto m = udiv_magic(d, w);
  ASSERT_TRUE(m.has_value());
  ASSERT_EQ(run_udiv(*m, x, w), x / d) << "w=" << w << " d=" << d << " x=" << x;
}

void check_sdiv(u64 d, unsigned w, i64 x) {
  auto m = sdiv_magic(d, w);
  ASSERT_TRUE(m.has_value());
  ASSERT_EQ(run_sdiv(*m, x), x / static_cast<i64>(d))
      << "w=" << w << " d=" << d << " x=" << x;
}

std::vector<u64> edge_xs(unsigned w, u64 d, std::mt19937_64 &rng) {
  std::vector<u64> xs;
  u64 mask = wmask(w);
  for (u64 base : {u64{0}, mask, mask >> 1, (mask >> 1) + 1, d, d * 2, d * 3}) {
    for (i64 off = -3; off <= 3; off++) {
      xs.push_back((base + static_cast<u64>(off)) & mask);
    }
  }
  for (u64 k = 1; k < 64 && d * k <= mask && d * k >= k; k += 1 + k / 8) {
    xs.push_back(d * k);
    xs.push_back((d * k - 1) & mask);
  }
  for (int i = 0; i < 64; i++) {
    xs.push_back(rng() & mask);
  }
  return xs;
}
}  // namespace

TEST(ConstDiv, UDiv8Exhaustive) {
  for (u64 d = 3; d < 128; d++) {
    if (std::has_single_bit(d)) {
      continue;
    }
    for (u64 x = 0; x < 256; x++) {
      check_udiv(d, 8, x);
    }
  }
}

TEST(ConstDiv, SDiv8Exhaustive) {
  for (u64 d = 3; d <= 128; d++) {
    if (std::has_single_bit(d)) {
      continue;
    }
    for (i64 x = -128; x < 128; x++) {
      check_sdiv(d, 8, x);
    }
  }
}

TEST(ConstDiv, UDiv16) {
  for (u64 d = 3; d < 32768; d += (d < 300 ? 1 : 7)) {
    if (std::has_single_bit(d)) {
      continue;
    }
    for (u64 x = 0; x < 65536; x += (d < 300 ? 1 : 5)) {
      check_udiv(d, 16, x);
    }
  }
}

TEST(ConstDiv, SDiv16) {
  for (u64 d = 3; d <= 32768; d += (d < 300 ? 1 : 7)) {
    if (std::has_single_bit(d)) {
      continue;
    }
    for (i64 x = -32768; x < 32768; x += (d < 300 ? 1 : 5)) {
      check_sdiv(d, 16, x);
    }
    check_sdiv(d, 16, -32768);
    check_sdiv(d, 16, 32767);
  }
}

TEST(ConstDiv, RandomWide) {
  std::mt19937_64 rng(42);
  for (unsigned w : {32u, 64u}) {
    u64 mask = wmask(w);
    for (int it = 0; it < 20000; it++) {
      u64 d;
      switch (it % 4) {
        case 0:
          d = 3 + rng() % 100;
          break;
        case 1:
          d = (rng() & mask) >> (rng() % (w - 1));
          break;
        default:
          d = rng() & (mask >> 1);
      }
      if (d < 3 || std::has_single_bit(d) || d > (mask >> 1)) {
        continue;
      }
      for (u64 x : edge_xs(w, d, rng)) {
        check_udiv(d, w, x);
        check_sdiv(d, w, sext(x, w));
      }
    }
  }
}

TEST(ConstDiv, ShortFormsAreFound) {
  // 32 bit division by 3 and 5 need no add fixup, 7 does
  auto m3 = udiv_magic(3, 32);
  ASSERT_TRUE(m3.has_value());
  EXPECT_FALSE(m3->add);
  EXPECT_EQ(m3->mul, 0xAAAAAAABull);
  EXPECT_EQ(m3->shift, 33);
  EXPECT_FALSE(udiv_magic(5, 32)->add);
  EXPECT_TRUE(udiv_magic(7, 32)->add);
  // even divisors shift first
  auto m14 = udiv_magic(14, 32);
  ASSERT_TRUE(m14.has_value());
  EXPECT_FALSE(m14->add);
  EXPECT_EQ(m14->pre_shift, 1);
  // signed division by 10 at 32 bit
  auto s10 = sdiv_magic(10, 32);
  ASSERT_TRUE(s10.has_value());
  EXPECT_EQ(s10->mul, 0x66666667ull);
  EXPECT_EQ(s10->shift, 34);
}
