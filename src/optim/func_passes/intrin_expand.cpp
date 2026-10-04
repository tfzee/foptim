#include "intrin_expand.hpp"
#include "config/compiler_config.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/AnalysisManager.hpp"
#include "optim/function_pass.hpp"
#include "optim/helper/inline.hpp"
#include <fmt/base.h>
namespace foptim::optim {

std::optional<fir::BasicBlock> IntrinExpand::expand_memset(fir::Function &func,
                                                           fir::BasicBlock bb,
                                                           fir::Instr instr) {
  if (!instr->is(fir::IntrinsicSubType::Memset)) {
    return {};
  }
  auto *ctx = func.ctx;

  // fmt::println("{:cd}\n==============", bb);
  auto ptr = instr->args[0];
  auto value = instr->args[1];
  auto num = instr->args[2];

  auto new_block = split_block(instr);

  fir::Builder buh{bb};
  auto loop_bb = buh.append_bb();
  auto curr_ptr = fir::ValueR{loop_bb.add_arg(
      ctx->storage.insert_bb_arg(loop_bb, ctx->get_ptr_type()))};
  auto curr_off = fir::ValueR{loop_bb.add_arg(
      ctx->storage.insert_bb_arg(loop_bb, ctx->get_int_type(64)))};
  buh.at_end(bb);
  auto b = buh.build_branch(loop_bb);
  b.add_bb_arg(0, ptr);
  b.add_bb_arg(0, fir::ValueR{ctx->get_constant_int(0, 64)});
  buh.at_end(loop_bb);

  buh.build_store(curr_ptr, value, false, false);
  auto next_ptr =
      buh.build_ptr_add(curr_ptr, fir::ValueR{ctx->get_constant_int(1, 64)});
  auto next_off =
      buh.build_int_add(curr_off, fir::ValueR{ctx->get_constant_int(1, 64)});
  auto cond = buh.build_int_cmp(next_off, num, fir::ICmpInstrSubType::ULT);
  auto loopb = buh.build_cond_branch(cond, loop_bb, new_block);
  loopb.add_bb_arg(0, next_ptr);
  loopb.add_bb_arg(0, next_off);
  instr.destroy();

  // fmt::println("{:cd}", func);
  return {new_block};
}

} // namespace foptim::optim
