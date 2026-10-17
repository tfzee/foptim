#include <gtest/gtest.h>

#include <random>

#include "utils/APInt.hpp"

namespace {
using namespace foptim;
using u128 = unsigned __int128;
using i128 = __int128;

u128 rnd128(std::mt19937_64 &g) {
  // mix of small, huge and edge values
  switch (g() % 6) {
    case 0:
      return g() % 8;
    case 1:
      return static_cast<u128>(g());
    case 2:
      return ~u128{0} - (g() % 4);
    case 3:
      return u128{1} << (g() % 128);
    default:
      return (static_cast<u128>(g()) << 64) | g();
  }
}

u128 mask(unsigned w) { return w == 128 ? ~u128{0} : ((u128{1} << w) - 1); }
i128 sext128(u128 x, unsigned w) {
  const unsigned sh = 128 - w;
  return static_cast<i128>(x << sh) >> sh;
}

void check_width(unsigned w, std::mt19937_64 &g, int iters) {
  const u128 m = mask(w);
  for (int it = 0; it < iters; it++) {
    const u128 x = rnd128(g) & m;
    const u128 y = rnd128(g) & m;
    const auto a = APInt::from_u128(w, x);
    const auto b = APInt::from_u128(w, y);
    ASSERT_EQ(a.to_u128(), x);
    ASSERT_EQ((a + b).to_u128(), (x + y) & m) << w;
    ASSERT_EQ((a - b).to_u128(), (x - y) & m) << w;
    ASSERT_EQ((a * b).to_u128(), (x * y) & m) << w;
    ASSERT_EQ((a & b).to_u128(), x & y);
    ASSERT_EQ((a | b).to_u128(), x | y);
    ASSERT_EQ((a ^ b).to_u128(), x ^ y);
    ASSERT_EQ((~a).to_u128(), ~x & m);
    ASSERT_EQ((-a).to_u128(), (u128{0} - x) & m);
    ASSERT_EQ(a == b, x == y);
    ASSERT_EQ(a.ult(b), x < y);
    ASSERT_EQ(a.ule(b), x <= y);
    ASSERT_EQ(a.slt(b), sext128(x, w) < sext128(y, w));
    ASSERT_EQ(a.sle(b), sext128(x, w) <= sext128(y, w));
    ASSERT_EQ(a.to_i128(), sext128(x, w));
    ASSERT_EQ(
        a.popcount(),
        static_cast<unsigned>(__builtin_popcountll(static_cast<u64>(x)) +
                              __builtin_popcountll(static_cast<u64>(x >> 64))));
    const unsigned amt = g() % (w + 3);
    ASSERT_EQ(a.shl(amt).to_u128(), amt >= w ? 0 : (x << amt) & m) << w;
    ASSERT_EQ(a.lshr(amt).to_u128(), amt >= w ? 0 : x >> amt) << w;
    ASSERT_EQ(a.ashr(amt).to_u128(),
              static_cast<u128>(sext128(x, w) >> std::min(amt, w - 1)) & m)
        << w << " " << amt;
    if (y != 0) {
      ASSERT_EQ(a.udiv(b).to_u128(), x / y) << w;
      ASSERT_EQ(a.urem(b).to_u128(), x % y) << w;
      const i128 sx = sext128(x, w);
      const i128 sy = sext128(y, w);
      if (!(sy == -1 && sx == sext128(u128{1} << (w - 1), w))) {
        ASSERT_EQ(a.sdiv(b).to_u128(), static_cast<u128>(sx / sy) & m) << w;
        ASSERT_EQ(a.srem(b).to_u128(), static_cast<u128>(sx % sy) & m) << w;
      }
    }
    bool ov = false;
    (void)a.uadd_ov(b, ov);
    ASSERT_EQ(ov, ((x + y) & m) < x);
    (void)a.usub_ov(b, ov);
    ASSERT_EQ(ov, x < y);
    (void)a.sadd_ov(b, ov);
    {
      const i128 s = sext128(x, w) + sext128(y, w);
      // only valid below 127 bits, otherwise skip
      if (w < 127) {
        ASSERT_EQ(ov, s != sext128(static_cast<u128>(s) & m, w));
      }
    }
    if (w <= 64) {
      (void)a.umul_ov(b, ov);
      ASSERT_EQ(ov, (x * y) > m);
      (void)a.smul_ov(b, ov);
      const i128 p = sext128(x, w) * sext128(y, w);
      ASSERT_EQ(ov, p != sext128(static_cast<u128>(p) & m, w));
    }
    // width changes
    const unsigned w2 = w + (g() % 200);
    ASSERT_EQ(a.zext(w2).trunc(w), a);
    ASSERT_EQ(a.sext(w2).trunc(w), a);
    ASSERT_EQ(a.sext(w2).is_neg(), a.is_neg());
    if (w2 <= 128) {
      ASSERT_EQ(a.sext(w2).to_i128(), sext128(x, w));
      ASSERT_EQ(a.zext(w2).to_u128(), x);
    }
  }
}

TEST(APIntTest, MatchesNativeInt128) {
  std::mt19937_64 g(1234);
  for (unsigned w :
       {1u, 2u, 7u, 8u, 31u, 32u, 33u, 63u, 64u, 65u, 100u, 127u, 128u}) {
    check_width(w, g, 3000);
  }
}

TEST(APIntTest, WideIdentities) {
  std::mt19937_64 g(99);
  for (unsigned w : {129u, 192u, 200u, 256u, 300u, 512u, 1000u}) {
    for (int it = 0; it < 500; it++) {
      auto gen = [&] {
        u64 words[16];
        for (auto &x : words) {
          x = g() % 3 == 0 ? 0 : g();
        }
        return APInt::from_words(w, words, 16);
      };
      const APInt a = gen();
      APInt b = gen();
      if (g() % 2) {
        b = b.lshr(g() % w);
      }
      if (b.is_zero()) {
        b = APInt(w, 1);
      }
      ASSERT_EQ((a + b) - b, a);
      ASSERT_EQ(a * b, b * a);
      ASSERT_EQ(a.udiv(b) * b + a.urem(b), a) << w;
      ASSERT_TRUE(a.urem(b).ult(b));
      ASSERT_EQ(a.sdiv(b) * b + a.srem(b), a) << w;
      const unsigned s = g() % w;
      ASSERT_EQ(a.shl(s), a * APInt(w, 1).shl(s));
      ASSERT_EQ(a.shl(s).lshr(s), a & APInt::all_ones(w).lshr(s));
      ASSERT_EQ(a.ashr(s).sext(w + 5), a.sext(w + 5).ashr(s));
      ASSERT_EQ(a.zext(w + 70).trunc(w), a);
      ASSERT_EQ(~a + APInt(w, 1), -a);
      ASSERT_EQ(a.ucmp(b) < 0, a.ult(b));
      ASSERT_EQ(a.scmp(b) < 0, a.slt(b));
      ASSERT_EQ(a.clz() + a.active_bits(), w);
      bool ov = false;
      const APInt p = a.umul_ov(b, ov);
      ASSERT_EQ(p, a * b);
      const APInt wide = a.zext(2 * w) * b.zext(2 * w);
      ASSERT_EQ(ov, wide.active_bits() > w);
      const APInt q = a.smul_ov(b, ov);
      ASSERT_EQ(q, a * b);
      ASSERT_EQ(ov, a.sext(2 * w) * b.sext(2 * w) != q.sext(2 * w));
    }
  }
}

TEST(APIntTest, Strings) {
  EXPECT_EQ(APInt(256, 1).shl(200).to_string(),
            "1606938044258990275541962092341162602522202993782792835301376");
  EXPECT_EQ(APInt::from_i64(8, -5).to_string(true), "-5");
  EXPECT_EQ(APInt::from_i64(8, -5).to_string(false), "251");
  EXPECT_EQ(APInt::from_i128(128, -1).to_string(false, 16),
            "ffffffffffffffffffffffffffffffff");
  EXPECT_EQ(APInt::zero(70).to_string(), "0");
  EXPECT_EQ(APInt::signed_min(128).to_string(true),
            "-170141183460469231731687303715884105728");
  EXPECT_EQ(APInt(8, 5).to_string(false, 2), "101");
}

TEST(APIntTest, CopyMove) {
  APInt a = APInt(300, 5).shl(250);
  APInt b = a;
  EXPECT_EQ(a, b);
  APInt c = std::move(b);
  EXPECT_EQ(a, c);
  b = c;
  EXPECT_EQ(b, a);
  APInt s(8, 200);
  s = a;
  EXPECT_EQ(s.bits(), 300u);
  EXPECT_EQ(s, a);
  EXPECT_EQ(a.hash(), s.hash());
  EXPECT_EQ(APInt::signed_max(65).to_string(true), "18446744073709551615");
  EXPECT_TRUE(APInt::all_ones(77).is_all_ones());
  for (u32 w : {1u, 8u, 64u, 65u, 130u}) {
    for (u32 n : {0u, 1u, w / 2, w}) {
      EXPECT_EQ(APInt::low_bits(w, n).popcount(), n);
      EXPECT_EQ(APInt::high_bits(w, n).popcount(), n);
      EXPECT_EQ(APInt::low_bits(w, n), ~APInt::high_bits(w, w - n));
    }
  }
  EXPECT_EQ(APInt(77, 8).ctz(), 3u);
  EXPECT_EQ(APInt(77, 8).log2(), 3u);
}
}  // namespace
