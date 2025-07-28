#include <gtest/gtest.h>

#include <optim/analysis/live_variables.hpp>
#include <utils/stable_vec.hpp>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/function_ref.hpp"
#include "optim/analysis/dominators.hpp"
#include "utils/logging.hpp"

using namespace foptim::utils;
using namespace foptim::fir;
using namespace foptim;

TEST(LivenessTest, SingleBB) {
  foptim::fir::Context ctx;

  auto func = ctx.data->create_function(
      "testFunc", ctx.data->get_func_ty(ctx.data->get_int_type(32),
                                        {ctx.data->get_int_type(32)}));

  auto builder = func.builder();
  auto entry_bb = func.func->get_entry();
  builder.at_end(entry_bb);
  auto x = builder.build_int_add(ValueR{entry_bb->args[0]},
                                 ValueR{entry_bb->args[0]});
  auto ret = builder.build_return(x);

  optim::CFG cfg{*func.func};
  optim::Dominators dom{cfg};
  optim::LiveVariables lives{*func.func, cfg};

  EXPECT_EQ(lives.live_variables.size(), 2);
  EXPECT_EQ(lives.live_variables[x].size(), 1);
  EXPECT_EQ(lives.live_variables[ValueR(entry_bb->args[0])].size(), 1);
  auto x_live = lives.live_variables[x][0];
  EXPECT_EQ(x_live.bb, 0);
  EXPECT_EQ(x_live.start, 1);
  EXPECT_EQ(x_live.end, 1);
  auto arg_live = lives.live_variables[ValueR(entry_bb->args[0])][0];
  EXPECT_EQ(arg_live.bb, 0);
  EXPECT_EQ(arg_live.bb, 0);
  EXPECT_EQ(arg_live.start, 0);
  EXPECT_EQ(arg_live.end, 0);

  EXPECT_TRUE(!lives.isLive(x, x.as_instr()));
  EXPECT_TRUE(lives.isLive(x, ret));
  // lives.dump();
}

TEST(LivenessTest, AcrossBB) {
  foptim::fir::Context ctx;

  auto func = ctx.data->create_function(
      "testFunc", ctx.data->get_func_ty(ctx.data->get_int_type(32),
                                        {ctx.data->get_int_type(32),
                                         ctx.data->get_int_type(32)}));

  auto builder = func.builder();
  auto entry_bb = func.func->get_entry();
  auto end_bb = builder.append_bb();
  end_bb.add_arg(
      ctx.data->storage.insert_bb_arg(end_bb, ctx.data->get_int_type(32)));

  builder.at_end(entry_bb);
  auto branch = builder.build_branch(end_bb);
  branch.add_bb_arg(0, ValueR{entry_bb->args[0]});
  builder.at_end(end_bb);
  auto x =
      builder.build_int_add(ValueR{entry_bb->args[0]}, ValueR{end_bb->args[0]});
  auto y = builder.build_int_add(ValueR{entry_bb->args[1]}, x);
  auto ret = builder.build_return(y);

  optim::CFG cfg{*func.func};
  optim::Dominators dom{cfg};
  optim::LiveVariables lives{*func.func, cfg};

  // lives.dump();

  EXPECT_EQ(lives.live_variables.size(), 5);
  EXPECT_EQ(lives.live_variables[ValueR(entry_bb->args[0])].size(), 2);
  EXPECT_EQ(lives.live_variables[ValueR(entry_bb->args[1])].size(), 2);
  EXPECT_EQ(lives.live_variables[ValueR(end_bb->args[0])].size(), 1);
  EXPECT_EQ(lives.live_variables[x].size(), 1);
  EXPECT_EQ(lives.live_variables[y].size(), 1);
  auto x_live = lives.live_variables[x][0];
  EXPECT_TRUE(x_live.bb == 1);
  EXPECT_TRUE(x_live.start == 1);
  EXPECT_TRUE(x_live.end == 1);

  auto y_live = lives.live_variables[y][0];
  EXPECT_TRUE(y_live.bb == 1);
  EXPECT_TRUE(y_live.start == 2);
  EXPECT_TRUE(y_live.end == 2);

  auto arg_live1 = lives.live_variables[ValueR(entry_bb->args[0])][0];
  auto arg_live2 = lives.live_variables[ValueR(entry_bb->args[0])][1];
  EXPECT_TRUE(arg_live1.bb == 0);
  EXPECT_TRUE(arg_live1.start == 0);
  EXPECT_TRUE(arg_live1.end == 1);
  EXPECT_TRUE(arg_live2.bb == 1);
  EXPECT_TRUE(arg_live2.start == 0);
  EXPECT_TRUE(arg_live2.end == 0);

  auto arg2_live1 = lives.live_variables[ValueR(entry_bb->args[1])][0];
  auto arg2_live2 = lives.live_variables[ValueR(entry_bb->args[1])][1];
  EXPECT_TRUE(arg2_live1.bb == 0);
  EXPECT_TRUE(arg2_live1.start == 0);
  EXPECT_TRUE(arg2_live1.end == 1);
  EXPECT_TRUE(arg2_live2.bb == 1);
  EXPECT_TRUE(arg2_live2.start == 0);
  EXPECT_TRUE(arg2_live2.end == 1);

  auto bb_arg_live = lives.live_variables[ValueR(end_bb->args[0])][0];
  EXPECT_TRUE(bb_arg_live.bb == 1);
  EXPECT_TRUE(bb_arg_live.start == 0);
  EXPECT_TRUE(bb_arg_live.end == 0);

  EXPECT_TRUE(!lives.isLive(x, x.as_instr()));
  EXPECT_TRUE(!lives.isLive(x, ret));
  EXPECT_TRUE(lives.isLive(x, y.as_instr()));
  EXPECT_TRUE(lives.isLive(y, ret));
}
