#pragma once
#include "../func.hpp"
#include "mir/analysis/cfg.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class CopyPropagation {
  void apply_impl(MFunc &func) {
    CFG cfg{func};
    TVec<ArgData> w_args;
    TVec<ArgData> helper;
    helper.reserve(4);
    w_args.reserve(4);

    for (size_t bb_id = 0; bb_id < func.bbs.size(); bb_id++) {
      auto &bb = func.bbs[bb_id];

      for (size_t instr_idp1 = bb.instrs.size(); instr_idp1 > 0; instr_idp1--) {
        const auto instr_id = instr_idp1 - 1;
        if (!bb.instrs[instr_id].is(GBaseSubtype::mov) ||
            !bb.instrs[instr_id].args[0].isReg() ||
            bb.instrs[instr_id].args[0].reg.is_concrete() ||
            !bb.instrs[instr_id].args[1].isReg() ||
            bb.instrs[instr_id].args[1].reg.is_concrete() ||
            bb.instrs[instr_id].args[0] == bb.instrs[instr_id].args[1] ||
            bb.instrs[instr_id].args[0].ty != bb.instrs[instr_id].args[1].ty) {
          continue;
        }
        auto output_arg = bb.instrs[instr_id].args[0];
        auto input_arg = bb.instrs[instr_id].args[1];
        const auto target_uid = reg_to_uid(output_arg.reg);
        // const auto input_uid = reg_to_uid(input_arg.reg);
        // if (!live._liveOut[bb_id][input_uid]) {
        // we only do it if the argument was live anyway so its free since
        //  we dont extend its lifetime
        // TODO: would also check other lifetime extensions stuff
        // continue;
        // }

        while (true) {
          // if we have some move we check each next use if we can propagate our
          // input value forwards
          auto next_use =
              find_next_use(bb.instrs, target_uid, instr_id + 1, helper);
          if (next_use.index == 0 || next_use.is_write ||
              bb.instrs[next_use.index].is(GBaseSubtype::invoke) ||
              bb.instrs[next_use.index].is(GBaseSubtype::call)) {
            break;
          }
          bool gets_overwritten = false;
          // TODO dont need to do full recheck technically
          // gotta double check that our input isnt getting overwritten on the
          // way
          for (auto i2 = instr_id + 1; i2 < next_use.index; i2++) {
            w_args.clear();
            written_args(bb.instrs[i2], w_args);
            for (auto w_arg : w_args) {
              if (w_arg.arg == input_arg) {
                gets_overwritten = true;
              }
            }
          }
          if (gets_overwritten) {
            break;
          }
          auto &target_instr = bb.instrs[next_use.index];
          // fmt::println("================================");
          // fmt::println("{} {} {}", next_use.index, next_use.is_read,
          //              next_use.is_write);
          // fmt::println("{:cd}", bb.instrs[instr_id]);
          // for (auto i = instr_id + 1; i < next_use.index - 1; i++) {
          //   fmt::println("{:cd}", bb.instrs[i]);
          // }
          // fmt::println("++++++++++++++++++++++++++++++++");
          // fmt::println("{:cd}", target_instr);
          bool modified = false;
          // find all the uses we know it cant be written since find_next_use
          // would have told us about that
          for (auto arg_id = 0; arg_id < target_instr.n_args; arg_id++) {
            if (target_instr.args[arg_id].uses_same_vreg(output_arg.reg)) {
              if (target_instr.args[arg_id].ty == output_arg.ty &&
                  target_instr.args[arg_id].reg == output_arg.reg) {
                target_instr.args[arg_id].reg = input_arg.reg;
                modified = true;
              }
              if (target_instr.args[arg_id].ty == output_arg.ty &&
                  target_instr.args[arg_id].indx == output_arg.reg) {
                target_instr.args[arg_id].indx = input_arg.reg;
                modified = true;
              }
            }
          }
          // fmt::println("{:cd}\n", target_instr);
          // if we failed to propagate the value we also need to break out of
          // this otherwise infinite loop
          if (!modified) {
            break;
          }
        }
      }
    }
  }

 public:
  void apply(MFunc &func) {
    ZoneScopedNC("CopyProp", COLOR_OPTIMF);
    apply_impl(func);
  }
  void apply(FVec<MFunc> &funcs) {
    for (auto &func : funcs) {
      apply(func);
    }
  }
};

}  // namespace foptim::fmir
