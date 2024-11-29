#include <gtest/gtest.h>
#include "test_bitset.hpp"
#include "test_stable_vec.hpp"
#include "test_ir_liveness.hpp"



int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}


