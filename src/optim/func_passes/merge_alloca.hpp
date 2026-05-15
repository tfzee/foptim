#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "optim/function_pass.hpp"
#include "utils/bitset.hpp"
#include <limits>

namespace foptim::optim {

// merges allocas for better codegen + less wasted stack space when allocating
// them
class MergeAllocaPass final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func, fir::BasicBlock bl) {
    (void)ctx;
    (void)func;

    // todo dont recreate this every time this is called
    TVec<std::tuple<fir::Instr, u32>> allocas;

    u32 total_alloca_size = 0;
    for (auto instr : bl->instructions) {
      if (instr->is(fir::InstrType::AllocaInstr)) {
        auto size = instr->args[0];
        if (!size.is_constant()) {
          continue;
        }
        allocas.emplace_back(instr, total_alloca_size);
        total_alloca_size += size.as_constant()->as_int();
      }
    }
    if (allocas.empty()) {
      return;
    }
    // fmt::println("{}", bl);

    auto bb = fir::Builder{bl};
    bb.at_start(bl);

    auto new_alloca = bb.build_alloca(
        fir::ValueR{ctx->get_constant_int(total_alloca_size, 32)});

    auto term = bl->get_terminator();
    for (auto [alloca, offset] : allocas) {
      // insert latest position within the first bb thats legal
      //  so if use in first bb generate there otherwise before the terminator
      // TODO: could also search in later bbs for first use(but need dominator
      // stuff for that) or generate duplicates of these in each bb that
      // accesses it
      auto insert_bef_loc = term;
      u64 lowest_index = std::numeric_limits<u64>::max();
      for (auto old_use : alloca->get_uses()) {
        if (old_use.user->get_parent() != bl) {
          continue;
        }
        auto bl_indx = bl->get_instr_id(old_use.user);
        if (bl_indx < lowest_index) {
          lowest_index = bl_indx;
          insert_bef_loc = old_use.user;
        }
      }
      bb.before(insert_bef_loc);
      auto new_value = bb.build_int_add(
          new_alloca, fir::ValueR{ctx->get_constant_int(offset, 32)});
      alloca->replace_all_uses(new_value);
    }
    // fmt::println("{}\n========================", bl);
  }
  void apply(fir::Context &ctx, fir::Function &func) override {
    for (auto bb : func.get_bbs()) {
      apply(ctx, func, bb);
    }
  }
};
} // namespace foptim::optim
