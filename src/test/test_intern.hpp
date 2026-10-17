#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "ir/context.hpp"

namespace {
using namespace foptim;

TEST(InternTest, SameValueSameRef) {
  fir::Context ctx{nullptr};
  const auto a = ctx->get_constant_int(42, 32);
  const auto b = ctx->get_constant_int(42, 32);
  const auto c = ctx->get_constant_int(42, 64);
  const auto d = ctx->get_constant_int(43, 32);
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
  EXPECT_NE(a, d);
  EXPECT_EQ(ctx->get_int_type(13), ctx->get_int_type(13));
  EXPECT_NE(ctx->get_int_type(13), ctx->get_int_type(14));
}

TEST(InternTest, FloatsCompareBitwise) {
  fir::Context ctx{nullptr};
  const auto f64 = ctx->get_float_type(64);
  const auto pz = ctx->get_constant_value(0.0, f64);
  const auto nz = ctx->get_constant_value(-0.0, f64);
  EXPECT_NE(pz, nz);
  EXPECT_EQ(pz, ctx->get_constant_value(0.0, f64));
  const double nan = std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ(ctx->get_constant_value(nan, f64),
            ctx->get_constant_value(nan, f64));
}

TEST(InternTest, ConcurrentRequestsAgree) {
  fir::Context ctx{nullptr};
  constexpr int N = 2000;
  constexpr int NThreads = 8;
  std::vector<std::vector<fir::ConstantValueR>> out(NThreads);
  std::vector<std::thread> threads;
  for (int t = 0; t < NThreads; t++) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < N; i++) {
        // every thread asks for the same values in a different order
        const int k = (i * 7 + t * 131) % N;
        out[t].push_back(ctx->get_constant_int(k, 17 + k % 3));
      }
    });
  }
  for (auto &th : threads) {
    th.join();
  }
  for (int t = 1; t < NThreads; t++) {
    for (int i = 0; i < N; i++) {
      const int k = (i * 7 + t * 131) % N;
      EXPECT_EQ(out[t][i], ctx->get_constant_int(k, 17 + k % 3));
    }
  }
  // no duplicates were created: asking again must not grow the storage
  size_t before = ctx->storage.storage_constant.n_used();
  for (int k = 0; k < N; k++) {
    (void)ctx->get_constant_int(k, 17 + k % 3);
  }
  EXPECT_EQ(before, ctx->storage.storage_constant.n_used());
}

TEST(InternTest, FreedConstantIsRecreated) {
  fir::Context ctx{nullptr};
  const auto a = ctx->get_constant_int(1234, 32);
  utils::SRef<fir::ConstantValue>(a)._invalidate();
  ctx->storage.storage_constant.collect_garbage();
  const auto b = ctx->get_constant_int(1234, 32);
  EXPECT_TRUE(b.is_valid());
  EXPECT_EQ(b.get_raw_ptr()->as_int(), 1234);
}
}  // namespace
