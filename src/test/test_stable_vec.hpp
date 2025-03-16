#include "utils/logging.hpp"
#include "utils/stable_vec_slot.hpp"
#include <gtest/gtest.h>
#include <utils/stable_vec.hpp>

using namespace foptim::utils;
using namespace foptim;

TEST(StableVecTest, BasicInsertDelete) {
  StableVec<u32> vec;
  auto r1 = vec.push_back(1);
  auto r2 = vec.push_back(2);
  auto r3 = vec.push_back(3);

  EXPECT_EQ(r1.is_valid(), true);
  EXPECT_EQ(r2.is_valid(), true);
  EXPECT_EQ(r3.is_valid(), true);
#ifdef SLOT_CHECK_GENERATION
  EXPECT_EQ(r1.generation, 1);
  EXPECT_EQ(r2.generation, 1);
  EXPECT_EQ(r3.generation, 1);
#endif

  EXPECT_EQ((u8 *)r1.get_raw_ptr() + sizeof(Slot<u32>), (u8 *)r2.get_raw_ptr());
  const auto *r1_ptr = r1.get_raw_ptr();
  vec.remove(r1);

  EXPECT_EQ(r1.data_ref->used, SlotState::FreeList);
  EXPECT_EQ(r1.is_valid(), false);
  EXPECT_EQ(r2.is_valid(), true);
  EXPECT_EQ(r3.is_valid(), true);

  auto r4 = vec.push_back(4);
#ifdef SLOT_CHECK_GENERATION
  EXPECT_EQ(r2.generation, 1);
  EXPECT_EQ(r4.generation, 2);
#endif
  EXPECT_EQ(r1_ptr, r4.get_raw_ptr());
}
