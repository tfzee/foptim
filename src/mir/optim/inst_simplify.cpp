#include "inst_simplify.hpp"
#include "mir/instr.hpp"

namespace foptim::fmir {

//@returns true if the instruction was deleted
static bool simplify(MInstr &instr, IRVec<MInstr> &instrs, size_t instr_id) {
  switch (instr.op) {
  case Opcode::mov:
  case Opcode::mov_zx: {
    if (instr.args[0] == instr.args[1]) {
      instrs.erase(instrs.begin() + instr_id);
      return true;
    }
    if (instr.args[0].isReg() && instr.args[1].isImm() &&
        instr.args[1].imm == 0) {
      instr.op = Opcode::lxor2;
      instr.n_args = 2;
      instr.args[1] = instr.args[0];
      return false;
    }
  }
  case Opcode::cmov: {
    if (instr.args[0] == instr.args[2]) {
      instrs.erase(instrs.begin() + instr_id);
      return true;
    }
    // if both inputs are the same replace iwth basic move
    if (instr.args[1] == instr.args[2]) {
      instr.op = Opcode::mov;
      instr.n_args = 2;
      return false;
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
