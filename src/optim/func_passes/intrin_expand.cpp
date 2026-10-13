#include "intrin_expand.hpp"
#include "config/compiler_config.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/function_pass.hpp"
#include "optim/helper/inline.hpp"
#include <fmt/base.h>
namespace foptim::optim {

namespace {
// The loops are do while loops so they need a skip when the count can be 0.
fir::Instr guarded_entry(fir::Builder &buh, fir::ContextData *ctx, fir::ValueR num,
                         fir::BasicBlock loop_bb, fir::BasicBlock skip_bb) {
  if (num.is_constant_int() && num.as_constant()->as_int() != 0) {
    return buh.build_branch(loop_bb);
  }
  auto not_empty = buh.build_int_cmp(
      num, fir::ValueR{ctx->get_constant_int(0, 64)},
      fir::ICmpInstrSubType::NE);
  return buh.build_cond_branch(not_empty, loop_bb, skip_bb);
}
} // namespace

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
  auto b = guarded_entry(buh, ctx, num, loop_bb, new_block);
  b.add_bb_arg(0, ptr);
  b.add_bb_arg(0, fir::ValueR{ctx->get_constant_int(0, 64)});
  buh.at_end(loop_bb);

  buh.build_store(curr_ptr, value, false, false);
  auto next_ptr =
      buh.build_ptr_add(curr_ptr, fir::ValueR{ctx->get_constant_int(
                                      value.get_type()->get_size(), 64)});
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

std::optional<fir::BasicBlock> IntrinExpand::expand_memcpy(fir::Function &func,
                                                           fir::BasicBlock bb,
                                                           fir::Instr instr) {
  if (!instr->is(fir::IntrinsicSubType::Memcpy)) {
    return {};
  }
  auto *ctx = func.ctx;

  auto dst = instr->args[0];
  auto src = instr->args[1];
  auto num = instr->args[2];
  auto elem_ty =
      instr->extra_type.is_valid() ? instr->extra_type : ctx->get_int_type(8);

  auto new_block = split_block(instr);

  fir::Builder buh{bb};
  auto loop_bb = buh.append_bb();
  auto curr_dst = fir::ValueR{loop_bb.add_arg(
      ctx->storage.insert_bb_arg(loop_bb, ctx->get_ptr_type()))};
  auto curr_src = fir::ValueR{loop_bb.add_arg(
      ctx->storage.insert_bb_arg(loop_bb, ctx->get_ptr_type()))};
  auto curr_off = fir::ValueR{loop_bb.add_arg(
      ctx->storage.insert_bb_arg(loop_bb, ctx->get_int_type(64)))};
  buh.at_end(bb);
  auto b = guarded_entry(buh, ctx, num, loop_bb, new_block);
  b.add_bb_arg(0, dst);
  b.add_bb_arg(0, src);
  b.add_bb_arg(0, fir::ValueR{ctx->get_constant_int(0, 64)});
  buh.at_end(loop_bb);

  // forward element by element, exactly what the loop this came from did
  auto loaded = buh.build_load(elem_ty, curr_src, false, false);
  buh.build_store(curr_dst, loaded, false, false);
  auto elem_size = fir::ValueR{ctx->get_constant_int(elem_ty->get_size(), 64)};
  auto next_dst = buh.build_ptr_add(curr_dst, elem_size);
  auto next_src = buh.build_ptr_add(curr_src, elem_size);
  auto next_off =
      buh.build_int_add(curr_off, fir::ValueR{ctx->get_constant_int(1, 64)});
  auto cond = buh.build_int_cmp(next_off, num, fir::ICmpInstrSubType::ULT);
  auto loopb = buh.build_cond_branch(cond, loop_bb, new_block);
  loopb.add_bb_arg(0, next_dst);
  loopb.add_bb_arg(0, next_src);
  loopb.add_bb_arg(0, next_off);
  instr.destroy();

  return {new_block};
}

static bool expand_to_libc_call(fir::Function &func, fir::Instr instr) {
  const bool is_memmove = instr->is(fir::IntrinsicSubType::Memmove);
  if (!is_memmove && !instr->is(fir::IntrinsicSubType::Memcmp)) {
    return false;
  }
  auto *ctx = func.ctx;
  auto callee = ctx->get_function(is_memmove ? "memmove" : "memcmp");

  fir::Builder buh{instr};
  auto len = instr->args[2];
  if (is_memmove && instr->memcpy_elem_size() != 1) {
    len = buh.build_int_mul(
        len, fir::ValueR{ctx->get_constant_int(instr->memcpy_elem_size(), 64)});
  }
  fir::ValueR args[3] = {instr->args[0], instr->args[1], len};
  auto call = buh.build_call(
      fir::ValueR{ctx->get_constant_value(callee)}, callee->func_ty,
      is_memmove ? ctx->get_ptr_type() : ctx->get_int_type(32), args);
  if (!is_memmove) {
    instr->replace_all_uses(call);
  }
  instr.destroy();
  return true;
}

PreservedAnalysis IntrinLowerCalls::apply(fir::Context & /*ctx*/,
                                          fir::Function &func) {
  ZoneScopedNC("IntrinLowerCalls", COLOR_OPTIMF);
  bool modified = false;
  for (auto bb : func.basic_blocks) {
    // the instruction is replaced by a call, iterate over a copy
    const auto instrs = bb->get_instrs();
    for (auto i : std::vector(instrs.begin(), instrs.end())) {
      if (i->is(fir::InstrType::Intrinsic)) {
        modified |= expand_to_libc_call(func, i);
      }
    }
  }
  return modified ? PreservedAnalysis::cfg_only() : PreservedAnalysis::all();
}

} // namespace foptim::optim
