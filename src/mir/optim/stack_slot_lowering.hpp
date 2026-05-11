
#pragma once
#include "config/compiler_config.hpp"
#include "mir/analysis/live_variables.hpp"
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
    utils::StatCollector::get().addi(func.extra_stack_slots.size(),
                                     "StackSlotsEmitted");
    if (stack_slots_size == 0) {
      return;
    }

    auto stack_ptr = MArgument{VReg::RSP(), Type::Int64};
    auto base_ptr = MArgument{VReg::RBP(), Type::Int64};
    func.bbs[0].instrs.insert(
        func.bbs[0].instrs.begin(),
        MInstr{GArithSubtype::add2, stack_ptr, stack_slots_size});
    for (auto &bb : func.bbs) {
      auto r = bb.instrs.end();
      if (!r->is(GBaseSubtype::ret)) {
        continue;
      }
      bb.instrs.insert(
          bb.instrs.end() - 1,
          MInstr{GArithSubtype::sub2, stack_ptr, stack_slots_size});
    }

    

    fmt::println("{}", func);
    fmt::println("{}", func.extra_stack_slots.size());
    fmt::println("{}", stack_slots_size);

    // for (auto &bb : func.bbs) {
    //   for (size_t i_id = 0; i_id < bb.instrs.size(); i_id++) {
    //     auto &i = bb.instrs[i_id];
    //   }
    // }
    TODO("impl");
  }
};
} // namespace foptim::fmir
