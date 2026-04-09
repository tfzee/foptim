#pragma once
#include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {
class TailRecElim final : public FunctionPass {
  fir::BasicBlock old_entry = fir::BasicBlock(fir::BasicBlock::invalid());
  fir::BasicBlock new_entry = fir::BasicBlock(fir::BasicBlock::invalid());
  fir::TypeR ret_type = fir::TypeR(fir::TypeR::invalid());

  void setup(fir::Context &ctx, fir::Function &func, fir::BasicBlock bb) {
    (void)bb;
    old_entry = func.get_entry();
    new_entry = ctx->storage.insert_bb(fir::BasicBlockData{&func});
    for (auto arg : old_entry->args) {
      new_entry.add_arg(ctx->storage.insert_bb_arg(new_entry, arg->get_type()));
    }
    fir::Builder b{new_entry};
    auto branch = b.build_branch(old_entry);
    for (auto arg : new_entry->args) {
      branch.add_bb_arg(0, fir::ValueR{arg});
    }
    ret_type = func.func_ty->as_func().return_type;
    func.basic_blocks.insert(func.basic_blocks.begin() + 0, new_entry);
  }

  void handle_basic(fir::Context &ctx, fir::Function &func,
                    fir::BasicBlock bb) {
    auto term = bb->get_terminator();
    auto call = bb->instructions[bb->instructions.size() - 2];
    // TODO: if all of the returns return the same constant we could still
    // apply it see:
    // https://github.com/llvm/llvm-project/blob/main/llvm/lib/Transforms/Scalar/TailRecursionElimination.cpp
    if (term->args.size() != 1 || term->args[0] != fir::ValueR{call}) {
      return;
    }
    if (!call->args[0].is_constant()) {
      return;
    }
    auto consti = call->args[0].as_constant();
    if (!consti->is_func()) {
      return;
    }
    auto funci = consti->as_func();
    if (funci.func != &func) {
      return;
    }
    setup(ctx, func, bb);

    fir::Builder b{new_entry};
    b.at_penultimate(bb);
    auto new_term = b.build_branch(old_entry);
    for (auto call_iter = std::next(call->args.begin());
         call_iter != call->args.end(); call_iter++) {
      new_term.add_bb_arg(0, *call_iter);
    }
    bb->get_terminator().destroy();
    call.destroy();
  }

  void handle_accum(fir::Context &ctx, fir::Function &func,
                    fir::BasicBlock bb) {
    auto term = bb->get_terminator();
    auto call = bb->instructions[bb->instructions.size() - 3];
    auto add = bb->instructions[bb->instructions.size() - 2];
    if (add->subtype != (u32)fir::BinaryInstrSubType::IntAdd &&
        add->subtype != (u32)fir::BinaryInstrSubType::IntSub) {
      return;
    }
    if (term->args.size() != 1 || term->args[0] != fir::ValueR{add}) {
      return;
    }
    u16 accum_rec_arg_id = add->args[0] == fir::ValueR{call} ? 1 : 0;
    if (add->args[0] != fir::ValueR{call} &&
        add->args[1] != fir::ValueR{call}) {
      return;
    }
    if (!call->args[0].is_constant()) {
      return;
    }
    auto consti = call->args[0].as_constant();
    if (!consti->is_func()) {
      return;
    }
    auto funci = consti->as_func();
    if (funci.func != &func) {
      return;
    }
    setup(ctx, func, bb);
    auto accum_type = ret_type;
    // add the default value for the accumulator
    auto bool_type = ctx->get_int_type(1);
    new_entry->get_terminator().add_bb_arg(
        0, fir::ValueR{ctx->get_constant_value(0, accum_type)});
    new_entry->get_terminator().add_bb_arg(
        0, fir::ValueR{ctx->get_constant_value(0, bool_type)});

    auto new_accum_bb_arg =
        old_entry.add_arg(ctx->storage.insert_bb_arg(old_entry, accum_type));
    auto has_entered_loop_cond =
        old_entry.add_arg(ctx->storage.insert_bb_arg(old_entry, bool_type));

    fir::Builder b{new_entry};
    b.at_penultimate(bb);
    auto new_term = b.build_branch(old_entry);
    for (auto call_iter = std::next(call->args.begin());
         call_iter != call->args.end(); call_iter++) {
      new_term.add_bb_arg(0, *call_iter);
    }
    new_term.add_bb_arg(0, fir::ValueR{add});
    new_term.add_bb_arg(0, fir::ValueR{ctx->get_constant_value(1, bool_type)});

    bb->get_terminator().destroy();

    // update all other returns to use the accumulator
    {
      for (auto bb : func.basic_blocks) {
        auto term = bb->get_terminator();
        if (!term->is(fir::InstrType::ReturnInstr)) {
          continue;
        }
        fir::Builder b{term};
        auto new_add = b.insert_copy(add);
        fmt::println("{}", new_add);
        new_add.replace_arg(accum_rec_arg_id, term->args[0]);
        // since we do not dominate this block potentially we need to add a
        // select
        //  to only choose the add version if we went trhough our loop
        //  otherwise the default return
        auto select = b.build_select(new_add.get_type(),
                                     fir::ValueR{has_entered_loop_cond},
                                     fir::ValueR{new_add}, term->args[0]);
        term.replace_arg(0, fir::ValueR{select});
      }
    }
    // should only have 1 use which is the add, this must be true otherwise we
    // would be used in the return which would exit earlier
    // and it cant be used in any other bb since none can be dominated by this
    call->replace_all_uses(fir::ValueR{new_accum_bb_arg});
    call.destroy();
  }

 public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    if (func.attribs.no_recurse || func.attribs.variadic) {
      return;
    }
    for (auto bb : func.basic_blocks) {
      for (auto instr : bb->instructions) {
        if (instr->is(fir::InstrType::AllocaInstr)) {
          // must ensure its not using parents stackframe!
          // if there isnt one we dont need to worry :)
          // also moves allocas out of initial block could lead to issues
          // further down the pipeline further down the pipeline
          return;
        }
      }
    }

    for (auto bb : func.basic_blocks) {
      if (bb->get_terminator()->is(fir::InstrType::ReturnInstr) &&
          bb->instructions.size() > 1 &&
          bb->instructions[bb->instructions.size() - 2]->is(
              fir::InstrType::CallInstr)) {
        handle_basic(ctx, func, bb);
      } else if (bb->get_terminator()->is(fir::InstrType::ReturnInstr) &&
                 bb->instructions.size() > 2 &&
                 bb->instructions[bb->instructions.size() - 3]->is(
                     fir::InstrType::CallInstr) &&
                 bb->instructions[bb->instructions.size() - 2]->is(
                     fir::InstrType::BinaryInstr)) {
        handle_accum(ctx, func, bb);
      }
    }
  }
};
}  // namespace foptim::optim
