#include "inst_simplify.hpp"
#include "mir/instr.hpp"

#include <Tracy/tracy/Tracy.hpp>

namespace foptim::fmir {

//@returns true if the instruction was deleted
static bool simplify(MInstr &instr, IRVec<MInstr> &instrs,
                     size_t instr_id) {
  switch (instr.op) {
  case Opcode::mov:
  case Opcode::mov_zx: {
    // utils::Debug << instr << "\n";
    if (instr.args[0] == instr.args[1]) {
      // utils::Debug << "    HIT\n";
      instrs.erase(instrs.begin() + instr_id);
      return true;
    }
  }
  default:
    break;
  }
  return false;
}

void InstSimplify::apply(FVec<MFunc> &funcs) {
  ZoneScopedN("InstSimplify");
  for (auto &func : funcs) {
    for (auto &bb : func.bbs) {
      for (size_t instr_id = 0; instr_id < bb.instrs.size(); instr_id++) {
        if (simplify(bb.instrs[instr_id], bb.instrs, instr_id)) {
          instr_id--;
        }
      }
    }
  }
}

} // namespace foptim::fmir
