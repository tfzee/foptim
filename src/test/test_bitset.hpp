#include <gtest/gtest.h>

#include <utils/bitset.hpp>

using namespace foptim::utils;
using namespace foptim;

TEST(BitSetTest, BasicSetIter) {
  constexpr size_t N_BITS = 6;
  foptim::utils::BitSet<u64> a_bb{N_BITS, false};
  a_bb[3].set(true);
  auto b_bb = a_bb;
  auto bb = std::move(b_bb);

  {
    EXPECT_EQ(bb._data[0], a_bb._data[0]);
    EXPECT_EQ(b_bb._data, nullptr);
  }

  {
    bb.reset(false);
    auto iter = bb.begin();
    EXPECT_EQ(iter, bb.end());
  }

  {
    bb.reset(true);
    auto iter = bb.begin();
    EXPECT_EQ(*iter, 0);
    ++iter;
    EXPECT_EQ(*iter, 1);
    ++iter;
    EXPECT_EQ(*iter, 2);
    ++iter;
    EXPECT_EQ(*iter, 3);
    ++iter;
    EXPECT_EQ(*iter, 4);
    ++iter;
    EXPECT_EQ(*iter, 5);
    ++iter;
    EXPECT_EQ(iter, bb.end());
  }

  {
    bb.reset(false);
    bb[1].set(true);
    bb[3].set(true);
    bb[5].set(true);
    auto iter = bb.begin();
    EXPECT_EQ(*iter, 1);
    ++iter;
    EXPECT_EQ(*iter, 3);
    ++iter;
    EXPECT_EQ(*iter, 5);
    ++iter;
    EXPECT_EQ(iter, bb.end());
  }
}

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
    EXPECT_EQ(bb == bb2, true);
  }
}
