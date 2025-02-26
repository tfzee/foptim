#include "bb_reordering.hpp"
#include "mir/instr.hpp"

namespace foptim::fmir {
void BBReordering::apply(MFunc &func) {
  TVec<u32> new_bb_vals;
  new_bb_vals.reserve(func.bbs.size());

  for (size_t bb1_i = 0; bb1_i < func.bbs.size(); bb1_i++) {

    if (func.bbs[bb1_i].instrs.empty()) {
      continue;
    }

    auto &t1 = func.bbs[bb1_i].instrs.back();
    if (t1.op == Opcode::ret) {
      continue;
    }

    if (t1.op != Opcode::jmp) {
      ASSERT(false);
    }
    u32 target_bb = t1.bb_ref;
    if (target_bb == bb1_i + 1) {
      continue;
    }

    bool is_fallthrough_elsewhere = false;
    for (size_t bb2_i = 0; bb2_i < func.bbs.size(); bb2_i++) {
      if (bb2_i != bb1_i && !func.bbs[bb2_i].instrs.empty()) {
        const auto &t2 = func.bbs[bb2_i].instrs.back();
        if (t2.bb_ref == target_bb) {
          is_fallthrough_elsewhere = true;
          break;
        }
      }
    }

    if (!is_fallthrough_elsewhere) {
      func.bbs.insert(func.bbs.begin() + bb1_i + 1,
                      MBB{std::move(func.bbs[target_bb].instrs)});

      new_bb_vals.clear();
      for (u32 i = 0; i < func.bbs.size(); i++) {
        new_bb_vals.push_back(i);
      }

      for (u32 i = bb1_i + 1; i < func.bbs.size(); i++) {
        new_bb_vals[i] = new_bb_vals[i] + 1;
      }
      new_bb_vals[target_bb] = bb1_i + 1;

      if (bb1_i + 1 <= target_bb) {
        func.bbs.erase(func.bbs.begin() + target_bb + 1);
        for (u32 i = target_bb + 1; i < func.bbs.size(); i++) {
          new_bb_vals[i] = new_bb_vals[i] - 1;
        }
      } else {
        func.bbs.erase(func.bbs.begin() + target_bb);
        for (u32 i = target_bb; i < func.bbs.size(); i++) {
          new_bb_vals[i] = new_bb_vals[i] - 1;
        }
      }

      for (auto &bb : func.bbs) {
        for (auto &instr : bb.instrs) {
          if (instr.has_bb_ref) {
            instr.bb_ref = new_bb_vals[instr.bb_ref];
          }
        }
      }
    }
  }
}

void BBReordering::apply(FVec<MFunc> &funcs) {
  ZoneScopedN("BB Reordering");
  // (void)funcs;
  for (auto &func : funcs) {
    apply(func);
  }
}
} // namespace foptim::fmir
