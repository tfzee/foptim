#include <gtest/gtest.h>

#include <utils/stable_vec.hpp>

#include "utils/stable_vec_slot.hpp"

TEST(StableVecTest, BasicInsertDelete) {
  using foptim::u32;
  using foptim::u8;
  foptim::utils::StableVec<u32> vec;
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

  EXPECT_EQ((u8 *)r1.get_raw_ptr() + sizeof(foptim::utils::Slot<u32>),
            (u8 *)r2.get_raw_ptr());
  const auto *r1_ptr = r1.get_raw_ptr();
  vec.remove(r1);

  EXPECT_EQ(r1.data_ref->state(), foptim::utils::SlotState::FreeList);
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

TEST(StableVecTest, SlotWordEncoding) {
  using foptim::u32;
  using foptim::utils::Slot;
  using foptim::utils::SlotState;
  // a zeroed (freshly memset) slot is a free list slot without generation
  Slot<u32> slot{};
  EXPECT_EQ(slot.state(), SlotState::FreeList);
  EXPECT_FALSE(slot.is_used());
  EXPECT_EQ(slot.generation(), 0);

  slot.publish(5);
  EXPECT_EQ(slot.state(), SlotState::Used);
  EXPECT_TRUE(slot.is_used());
#if SLOT_CHECK_LEVEL >= 2
  EXPECT_EQ(slot.generation(), 5);
#else
  EXPECT_EQ(slot.generation(), 0);
#endif

  // freeing keeps the generation but is no longer Used
  const u32 gen = slot.generation();
  slot.mark_free();
  EXPECT_EQ(slot.state(), SlotState::Free);
  EXPECT_FALSE(slot.is_used());
  EXPECT_EQ(slot.generation(), gen);

  EXPECT_TRUE(slot.try_collect());
  EXPECT_EQ(slot.state(), SlotState::FreeList);
  EXPECT_EQ(slot.generation(), 0);
  // only Free slots can be collected
  EXPECT_FALSE(slot.try_collect());
  slot.publish(1);
  EXPECT_FALSE(slot.try_collect());
  EXPECT_TRUE(slot.is_used());
}

TEST(StableVecTest, InvalidatedRefIsNotValid) {
  using foptim::u32;
  foptim::utils::StableVec<u32> vec;
  auto r1 = vec.push_back(1);
  auto r2 = vec.push_back(2);
  r1._invalidate();
  EXPECT_FALSE(r1.is_valid());
  EXPECT_EQ(r1.data_ref->state(), foptim::utils::SlotState::Free);
  EXPECT_TRUE(r2.is_valid());
  // a Free slot is not handed out again before it is collected
  auto r3 = vec.push_back(3);
  EXPECT_NE(r3.data_ref, r1.data_ref);
  vec.collect_garbage();
  EXPECT_EQ(r1.data_ref->state(), foptim::utils::SlotState::FreeList);
  EXPECT_FALSE(r1.is_valid());
  auto r4 = vec.push_back(4);
  EXPECT_EQ(r4.data_ref, r1.data_ref);
  EXPECT_TRUE(r4.is_valid());
#if SLOT_CHECK_LEVEL >= 2
  // the slot is reused but the stale ref must not match the new generation
  EXPECT_FALSE(r1.is_valid());
#endif
}

TEST(StableVecTest, NullRefIsNotValid) {
  foptim::utils::SRef<foptim::u32> null_ref{nullptr};
  EXPECT_FALSE(null_ref.is_valid());
  EXPECT_FALSE(foptim::utils::SRef<foptim::u32>::invalid().is_valid());
}

TEST(StableVecTest, IterationSkipsUnusedSlots) {
  using foptim::u32;
  foptim::utils::StableVec<u32> vec;
  auto r1 = vec.push_back(1);
  auto r2 = vec.push_back(2);
  auto r3 = vec.push_back(3);
  vec.remove(r2);
  u32 sum = 0;
  u32 n = 0;
  for (auto ref : vec) {
    sum += *ref.get_raw_ptr();
    n++;
  }
  EXPECT_EQ(n, 2);
  EXPECT_EQ(sum, 4);
  EXPECT_TRUE(r1.is_valid());
  EXPECT_TRUE(r3.is_valid());
}
