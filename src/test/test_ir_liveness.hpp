#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/function_ref.hpp"
#include "optim/analysis/dominators.hpp"
#include "utils/logging.hpp"
#include <gtest/gtest.h>
#include <optim/analysis/live_variables.hpp>
#include <utils/stable_vec.hpp>

using namespace foptim::utils;
using namespace foptim::fir;
using namespace foptim;


TEST(LivenessTest, BasicLiveness) {
  foptim::fir::Context ctx;

  auto func = ctx.data->create_function(
      "testFunc", ctx.data->get_func_ty(ctx.data->get_int_type(32), {ctx.data->get_int_type(32)}));

  auto builder = func.builder();
  auto entry_bb = func.func->get_entry();
  builder.at_end(entry_bb);
  auto x = builder.build_int_add(ValueR{entry_bb, 0}, ValueR{entry_bb, 0});
  auto ret = builder.build_return(x);

  optim::CFG cfg{*func.func};
  optim::Dominators dom{cfg};
  optim::LiveVariables lives{*func.func, cfg};

  auto x_live = lives.live_variables[x][0];
  EXPECT_TRUE(x_live.bb == 0);
  EXPECT_TRUE(x_live.start == 1);
  EXPECT_TRUE(x_live.end == 1);
  auto arg_live = lives.live_variables[ValueR(entry_bb, 0)][0];
  EXPECT_TRUE(arg_live.bb == 0);
  EXPECT_TRUE(arg_live.start == 0);
  EXPECT_TRUE(arg_live.end == 0);

  EXPECT_TRUE(!lives.isLive(x, x.as_instr()));
  EXPECT_TRUE(lives.isLive(x, ret));
  // lives.dump();
}
