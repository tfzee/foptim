
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
  void apply(MFunc &func, const conf::CompConf & /*config*/) override {

    u64 stack_slots_size = 0;
    TVec<u64> index_to_mem_off;
    for (auto &slot : func.extra_stack_slots) {
      index_to_mem_off.push_back(stack_slots_size);
      stack_slots_size += slot.size;
    }
    if (stack_slots_size == 0) {
      return;
    }

    // TODO: gotta handle generic CC
    stack_slots_size += stack_slots_size % 16;
    ASSERT(stack_slots_size % 16 == 0);
    utils::StatCollector::get().addi(
        static_cast<i64>(func.extra_stack_slots.size()), "StackSlotsEmitted");
    utils::StatCollector::get().addi(static_cast<i64>(stack_slots_size),
                                     "StackSlotsSize");

    auto stack_ptr = MArgument{VReg::RSP(), Type::Int64};

    func.bbs[0].instrs.insert(
        func.bbs[0].instrs.begin(),
        MInstr{GArithSubtype::sub2, stack_ptr, stack_slots_size});
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
            auto off = index_to_mem_off.at(arg.imm - 1);
            if (arg.scale == 8) {
              arg = MArgument::MemOB(off, VReg::RBP(), Type::Int64);
            } else if (arg.scale == 4) {
              arg = MArgument::MemOB(off, VReg::RBP(), Type::Int32);
            } else if (arg.scale == 2) {
              arg = MArgument::MemOB(off, VReg::RBP(), Type::Int16);
            } else if (arg.scale == 1) {
              arg = MArgument::MemOB(off, VReg::RBP(), Type::Int8);
            } else {
              fmt::println("{}", i);
              fmt::println("{}", arg);
              ASSERT(false);
            }
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
