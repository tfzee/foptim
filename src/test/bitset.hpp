#include "utils/logging.hpp"
#include <gtest/gtest.h>
#include <utils/bitset.hpp>

using namespace foptim::utils;
using namespace foptim;

TEST(BitSetTest, BasicSetGet) {
  constexpr size_t N_BITS = 68;
  foptim::utils::BitSet<u64> bb{N_BITS, false};
  foptim::utils::BitSet<u64> bb2{N_BITS, true};
  foptim::utils::BitSet<u64> bb3{N_BITS, false};

  auto n_elems = (N_BITS + sizeof(u64) * 8) / (sizeof(u64) * 8);

  ASSERT_EQ(n_elems, 2);

  EXPECT_EQ(bb[5], false);
  bb[5].set(true);
  EXPECT_EQ(bb[5], true);
  bb[5].set(false);
  EXPECT_EQ(bb[5], false);

  bb[5].set(true);
  {
    bb.mul(bb2);
    EXPECT_EQ(bb[5], true);
    bb.add(bb2);
    EXPECT_EQ(bb[5], true);
    bb.mul(bb);
    EXPECT_EQ(bb[5], true);
  }
  {
    bb.xor_(bb3);
    EXPECT_EQ(bb[5], true);
    bb.add(bb3);
    EXPECT_EQ(bb[5], true);
    bb.mul(bb3);
    EXPECT_EQ(bb[5], false);
  }
  {
    bb.reset(true);
    auto bb_copy = bb;
    bb_copy[5].set(false);
    EXPECT_EQ(bb[5], true);
    EXPECT_EQ(bb_copy[5], false);

    auto *old_ptr = bb_copy._data;
    auto bb_moved = std::move(bb_copy);
    EXPECT_EQ(bb_copy._data, nullptr);
    EXPECT_EQ(bb_moved._data, old_ptr);
  }
  {
    bb.reset(false);
    bb2.reset(true);
    for (size_t i = 0; i < N_BITS; i++) {
      bb[i].set(true);
    }
    // utils::Debug << bb << "\n" << bb2 << "\n";

    // u16 n_dead_bits = (n_elems * sizeof(u64) * 8) - N_BITS;
    // u16 n_alive_bits = sizeof(u64) * 8 - n_dead_bits;

    // auto mask = (((u64)1 << n_alive_bits) - 1);

    // for (size_t i = 0; i < 64; i++) {
    //   utils::Debug << ((mask >> i) & 1);
    // }
    // utils::Debug << "\n";
    EXPECT_EQ(bb == bb2, true);
  }
}
