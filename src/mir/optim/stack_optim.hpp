#pragma once
#include "mir/analysis/live_variables.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "utils/stats.hpp"

namespace foptim::fmir {

class StackOptim {
 public:
  void apply(MFunc& funcs) {
    TVec<ArgData> helper;
    for (auto& bb : funcs.bbs) {
      for (size_t i_id = 0; i_id < bb.instrs.size(); i_id++) {
        auto& i = bb.instrs[i_id];
        if (i.is(GArithSubtype::add2) && i.args[0].isReg() &&
            i.args[0].reg.is_concrete() && i.args[0].reg.c_reg() == CReg::SP &&
            i.args[1].isImm()) {
          auto next_use =
              find_next_use(bb.instrs, reg_to_uid(CReg::SP), i_id + 1, helper);
          if (next_use.index == 0 ||
              (!next_use.is_read && !next_use.is_write)) {
            continue;
          }
          auto& i2 = bb.instrs[next_use.index];
          if (i2.is(GArithSubtype::sub2) && i2.args[0].isReg() &&
              i2.args[0].reg.is_concrete() &&
              i2.args[0].reg.c_reg() == CReg::SP && i2.args[1].isImm()) {
            if (i.args[1].imm == i2.args[1].imm) {
              bb.instrs.erase(bb.instrs.begin() + next_use.index);
              bb.instrs.erase(bb.instrs.begin() + i_id);
              utils::StatCollector::get().addi(
                  1, "NStackAddSubElim", utils::StatCollector::StatMirOptim);
              i_id = 0;
              continue;
            } else {
              fmt::println("{:cd}", i);
              fmt::println("{:cd}", i2);
              fmt::println("{}", i.args[1].imm - i2.args[1].imm);
              TODO("impl");
            }
          }
        }
        if (i.is(GBaseSubtype::push)) {
          auto next_use =
              find_next_use(bb.instrs, reg_to_uid(CReg::SP), i_id + 1, helper);
          if (next_use.index == 0 ||
              (!next_use.is_read && !next_use.is_write)) {
            continue;
          }
          auto& i2 = bb.instrs[next_use.index];
          if (i2.is(GBaseSubtype::pop) && i2.args[0] == i.args[0]) {
            // fmt::println("{:cd}", i);
            // for (size_t i = i_id; i < next_use.index; i++) {
            //   fmt::println(">{:cd}", bb.instrs[i]);
            // }
            // fmt::println("{:cd}", i2);
            fmt::println("TODO: can forward push/pop here prob implement it dont be lazy ");
            // TODO("idk1 if really possbile");
          }
        }
        if (i.is(GBaseSubtype::pop) && i.args[0].isReg()) {
          auto next_use =
              find_next_use(bb.instrs, reg_to_uid(CReg::SP), i_id + 1, helper);
          if (next_use.index == 0 ||
              (!next_use.is_read && !next_use.is_write)) {
            continue;
          }
          auto& i2 = bb.instrs[next_use.index];
          if (i2.is(GBaseSubtype::push) && i2.args[0] == i.args[0]) {
            auto has_any_use =
                find_next_use(bb.instrs, reg_to_uid(i.args[0].reg), i_id + 1,
                              helper, next_use.index);
            bool gets_overwritten =
                has_any_use.index != 0 && has_any_use.is_write;
            if (has_any_use.index != 0 ||
                (has_any_use.is_write && !has_any_use.is_read)) {
              continue;
            }
            utils::StatCollector::get().addi(
                1, "NStackPopPushElim", utils::StatCollector::StatMirOptim);
            bb.instrs.erase(bb.instrs.begin() + next_use.index);
            if (gets_overwritten) {
              bb.instrs.erase(bb.instrs.begin() + i_id);
            } else {
              bb.instrs[i_id] =
                  MInstr{GBaseSubtype::mov, i.args[0],
                         MArgument::MemB(VReg::RSP(), i.args[0].ty)};
            }
            i_id = 0;
            continue;
          }
        }
      }
    }
  }
};
}  // namespace foptim::fmir
