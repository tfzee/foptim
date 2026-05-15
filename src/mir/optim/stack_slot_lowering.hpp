
#pragma once
#include "config/compiler_config.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "mir/optim/function_pass.hpp"
#include "utils/stats.hpp"
#include <fmt/base.h>

namespace foptim::fmir {

class StackSlotLowering : public FunctionPass {
public:
  void apply(MFunc &func, const conf::CompConf &) {

    u64 stack_slots_size = 0;
    for (auto &slot : func.extra_stack_slots) {
      stack_slots_size += slot.size;
    }
    if (stack_slots_size == 0) {
      return;
    }

    // TODO: gotta handle generic CC
    stack_slots_size += stack_slots_size % 16;
    ASSERT(stack_slots_size % 16 == 0);
    utils::StatCollector::get().addi(func.extra_stack_slots.size(),
                                     "StackSlotsEmitted");
    utils::StatCollector::get().addi(stack_slots_size, "StackSlotsSize");

    auto stack_ptr = MArgument{VReg::RSP(), Type::Int64};
    func.bbs[0].instrs.insert(
        func.bbs[0].instrs.begin(),
        MInstr{GArithSubtype::sub2, stack_ptr, stack_slots_size});
    // fmt::println("Entry");
    // fmt::println("{:cd}", func.bbs[0]);
    for (auto &bb : func.bbs) {
      auto r = bb.instrs.back();
      if (!r.is(GBaseSubtype::ret)) {
        continue;
      }
      bb.instrs.insert(
          bb.instrs.end() - 1,
          MInstr{GArithSubtype::add2, stack_ptr, stack_slots_size});
    }

    for (auto &bb : func.bbs) {
      for (auto &i : bb.instrs) {
        for (size_t arg_id = 0; arg_id < i.n_args; arg_id++) {
          auto &arg = i.args[arg_id];
          if (arg.isStackSlot()) {
            // TOOD: support other types
            ASSERT(arg.scale == 8);
            arg = MArgument::MemOB((arg.imm - 1) * 8, VReg::RBP(), Type::Int64);
          }
        }
      }
    }
    // fmt::println("{:cd}", func.bbs[0]);

    // fmt::println("{}", func);
    // fmt::println("{}", func.extra_stack_slots.size());
    // fmt::println("{}", stack_slots_size);

    // for (auto &bb : func.bbs) {
    //   for (size_t i_id = 0; i_id < bb.instrs.size(); i_id++) {
    //     auto &i = bb.instrs[i_id];
    //   }
    // }
    // TODO("impl");
  }
};
} // namespace foptim::fmir
