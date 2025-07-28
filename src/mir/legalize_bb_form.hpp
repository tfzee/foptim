#pragma once
#include "func.hpp"
#include "mir/instr.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

// when matching the the instrution it will generate illegal bbs
//  cause of conditional jumps since we first generate the bb_args for the first
//  case then a cjmp then the bb_args for the other case and then a jmp to the
//  other case we split this bb here to make it legal basicblocks again that
//  haven no control flow in them

class LegalizeBBForm {
  void apply(MFunc &func) {
    u32 start_instr;
    // find instructions between controlflow instructions
    //  so we can move them into their own block and rewire the jump to that
    //  block
    // bool moved = false;
    for (size_t bb_id = 0; bb_id < func.bbs.size(); bb_id++) {
      start_instr = func.bbs[bb_id].instrs.size();
      for (size_t instr_idp1 = func.bbs[bb_id].instrs.size(); instr_idp1 > 0;
           instr_idp1--) {
        auto instr_id = instr_idp1 - 1;
        // if we find a control flow instruction
        //  and there are instructions between this instruction and the last
        //  instruction we need to move them
        if (MInstr::is_control_flow(func.bbs[bb_id].instrs[instr_id].bop,
                                    func.bbs[bb_id].instrs[instr_id].sop)) {
          if (instr_id + 1 != start_instr) {
            // if (!moved) {
            //   fmt::println("=============LEG============");
            //   fmt::println("{}", func);
            // }
            // moved = true;
            // fmt::println("Moving {} {} {}", bb_id, instr_id + 1,
            //              start_instr - 1);
            // move the instructions between(exclusive range) instr_id and
            // start_instr into their own block
            auto &new_bb = func.bbs.emplace_back();
            auto &old_bb = func.bbs[bb_id];
            auto old_bb_ref = old_bb.instrs[start_instr].bb_ref;

            // only last cf instruction can be ret
            auto old_term = old_bb.instrs[start_instr];
            bool is_ret = old_bb.instrs[start_instr].is(GBaseSubtype::ret);
            // update to jump to new bb
            if (!is_ret) {
              old_bb.instrs[start_instr].bb_ref = func.bbs.size() - 1;
            }
            for (auto rem_id = instr_id + 1; rem_id < start_instr; rem_id++) {
              new_bb.instrs.push_back(old_bb.instrs[instr_id + 1]);
              func.bbs[bb_id].instrs.erase(old_bb.instrs.begin() + instr_id +
                                           1);
            }
            // add a jump in teh new bb to jump to the original jumps target
            if (is_ret) {
              new_bb.instrs.push_back(old_term);
            } else {
              new_bb.instrs.push_back(MInstr::jmp(old_bb_ref));
            }
          }
          start_instr = instr_id;
        }
      }
    }
    // if (moved) {
    //   fmt::println("{}", func);
    // }
  }

 public:
  void apply(FVec<MFunc> &funcs) {
    for (auto &f : funcs) {
      apply(f);
    }
  }
};

}  // namespace foptim::fmir
