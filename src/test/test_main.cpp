#include <gtest/gtest.h>

#include "test_access_meta.hpp"
#include "test_alias.hpp"
#include "test_apint.hpp"
#include "test_bitset.hpp"
#include "test_const_div.hpp"
#include "test_general.hpp"
#include "test_int128.hpp"
#include "test_intern.hpp"
#include "test_ir_liveness.hpp"
#include "test_loop_analysis.hpp"
#include "test_matcher_pm.hpp"
#include "test_mem_access.hpp"
#include "test_memory_ssa.hpp"
#include "test_multi_result.hpp"
#include "test_provenance.hpp"
#include "test_reg_alloc_solver.hpp"
#include "test_arg_gain.hpp"
#include "test_region_extractor.hpp"
#include "test_stable_vec.hpp"
#include "test_vec_selector.hpp"
#include "test_wide_int.hpp"

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
