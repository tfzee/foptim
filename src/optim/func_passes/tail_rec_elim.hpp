#pragma once
#include "ir/basic_block.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {
class TailRecElim final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    if (func.no_recurse) {
      return;
    }
    for (auto bb : func.basic_blocks) {
      if (!bb->get_terminator()->is(fir::InstrType::ReturnInstr) ||
          bb->instructions.size() <= 1 ||
          !bb->instructions[bb->instructions.size() - 2]->is(
              fir::InstrType::CallInstr)) {
        continue;
      }
      auto call = bb->instructions[bb->instructions.size() - 2];
      if (!call->args[0].is_constant()) {
        continue;
      }
      auto consti = call->args[0].as_constant();
      if (!consti->is_func()) {
        continue;
      }
      auto funci = consti->as_func();
      if (funci.func != &func) {
        continue;
      }
      fmt::println("TAIL CALL {}", func);

      auto old_entry = func.get_entry();
      auto new_bb = ctx->storage.insert_bb(fir::BasicBlockData{&func});
      for (auto arg : old_entry->args) {
        new_bb.add_arg(ctx->storage.insert_bb_arg(new_bb, arg->get_type()));
      }
      fir::Builder b{new_bb};
      auto branch = b.build_branch(old_entry);
      for (auto arg : new_bb->args) {
        branch.add_bb_arg(0, fir::ValueR{arg});
      }
      func.basic_blocks.insert(func.basic_blocks.begin() + 0, new_bb);

      b.at_penultimate(bb);
      auto new_term = b.build_branch(old_entry);
      for (auto call_iter = std::next(call->args.begin());
           call_iter != call->args.end(); call_iter++) {
        new_term.add_bb_arg(0, *call_iter);
      }
      bb->get_terminator().destroy();
      call.destroy();
    }
  }
};
} // namespace foptim::optim
