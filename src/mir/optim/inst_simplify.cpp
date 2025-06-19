#include "inst_simplify.hpp"
#include "mir/instr.hpp"
#include <cmath>
#include <fmt/core.h>

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
    if (instr.args[0].isReg() && instr.args[1].isImm()) {
      bool is_fp64 = instr.args[1].ty == Type::Float32;
      bool is_fp = instr.args[1].is_fp();
      bool is_zero = false;
      is_zero |= is_fp64 && instr.args[1].immf == 0. &&
                 !std::signbit(instr.args[1].immf);
      is_zero |= !is_fp && instr.args[1].imm == 0;

      if (is_zero && !instr.args[0].reg.is_vec_reg()) {
        instr.op = Opcode::lxor2;
        instr.n_args = 2;
        instr.args[1] = instr.args[0];
        return false;
      }
      if (is_zero && instr.args[0].reg.is_vec_reg()) {
        instr.op = Opcode::fxor;
        instr.n_args = 3;
        instr.args[1] = instr.args[0];
        instr.args[2] = instr.args[0];
        return false;
      }
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
//@returns true if the instruction was deleted
static bool early_simplify(MInstr &instr, IRVec<MInstr> &instrs,
                           size_t instr_id) {
  switch (instr.op) {
  case Opcode::mov:
  case Opcode::mov_zx: {
    if (instr.args[0] == instr.args[1] && instr.args[0].isReg() &&
        !instr.args[0].reg.is_concrete()) {
      instrs.erase(instrs.begin() + instr_id);
      return true;
    }
  }
  default:
    break;
  }
  return false;
}

static bool early_multi_simplify(IRVec<MInstr> &instrs, size_t instr_id) {
  // if (instr_id + 1 < instrs.size() && instrs[instr_id + 0].op == Opcode::lea
  // &&
  //     instrs[instr_id + 1].op == Opcode::mov) {
  //   auto &i1 = instrs[instr_id + 0];
  //   auto &i2 = instrs[instr_id + 1];
  //   // x = lea(global)
  //   // z = [x]
  // }
  if (instr_id + 2 < instrs.size() && instrs[instr_id + 0].op == Opcode::mov &&
      (instrs[instr_id + 1].op == Opcode::add2 ||
       instrs[instr_id + 1].op == Opcode::sub2) &&
      instrs[instr_id + 2].op == Opcode::mov) {
    auto &i1 = instrs[instr_id + 0];
    auto &i2 = instrs[instr_id + 1];
    auto &i3 = instrs[instr_id + 2];
    // fmt::println("   Matching? {} {} {}", i1, i2, i3);
    // x = y
    // x += _
    // y = x
    if (i1.args[0] == i2.args[0] && i1.args[0] == i3.args[1] &&
        i1.args[1] == i3.args[0] && i1.args[0] != i1.args[1] &&
        i2.args[1].isImm()) {
      i3.op = i2.op;
      i3.args[1] = i2.args[1];

      // cjmp x _
      if (instr_id + 3 < instrs.size()) {
        auto &i4 = instrs[instr_id + 3];
        // TODO: expand
        if ((i4.op == Opcode::cjmp_int_eq || i4.op == Opcode::cjmp_int_slt ||
             i4.op == Opcode::cjmp_int_ult || i4.op == Opcode::cjmp_int_sgt ||
             i4.op == Opcode::cjmp_int_ugt || i4.op == Opcode::cjmp_int_ne ||
             i4.op == Opcode::cjmp_int_sle || i4.op == Opcode::cjmp_int_ule ||
             i4.op == Opcode::cjmp_int_sge || i4.op == Opcode::cjmp_int_uge) &&
            i4.args[0] == i1.args[0]) {
          i4.args[0] = i1.args[1];
        }
      }
    }
  }
  return false;
}

static bool multi_simplify(IRVec<MInstr> &instrs, size_t instr_id) {
  if (instr_id + 1 < instrs.size() &&
      ((instrs[instr_id + 0].op == Opcode::add2 &&
        instrs[instr_id + 1].op == Opcode::sub2) ||
       (instrs[instr_id + 0].op == Opcode::sub2 &&
        instrs[instr_id + 1].op == Opcode::add2))) {
    auto a0 = instrs[instr_id + 0];
    auto a1 = instrs[instr_id + 1];
    if (a0.args[0] == a1.args[0] && a0.args[1] == a1.args[1]) {
      instrs.erase(instrs.begin() + instr_id, instrs.begin() + instr_id + 2);
      return true;
    }
  }
  // if (instr_id + 1 < instrs.size() && instrs[instr_id + 0].op ==
  // Opcode::push) {
  //   bool found = false;
  //   auto i2 = instr_id + 1;
  //   fmt::println("{}", instrs[instr_id]);
  //   for (; i2 < instrs.size(); i2++) {
  //     fmt::println("   {}", instrs[i2]);
  //     if (instrs[i2].op == Opcode::pop) {
  //       if (instrs[i2].args[0] == instrs[instr_id].args[0]) {
  //         found = true;
  //       }
  //       break;
  //     }
  //     if (instrs[i2].op == Opcode::push) {
  //       break;
  //     }
  //     if (instrs[i2].op == Opcode::call || instrs[i2].op == Opcode::invoke) {
  //       break;
  //     }
  //     bool breakit = false;
  //     for (auto arg : instrs[i2].args) {
  //       if (arg.isReg() && arg.reg.is_concrete() &&
  //           arg.reg.conc.creg == CReg::SP) {
  //         breakit = true;
  //         break;
  //       }
  //     }
  //     if (breakit) {
  //       break;
  //     }
  //   }
  //   if (found) {
  //     fmt::println("{}", instrs[instr_id]);
  //     fmt::println("{}", instrs[i2]);
  //     TODO("Okak");
  //   }
  // }
  return false;
}

void InstSimplify::apply(FVec<MFunc> &funcs) {
  ZoneScopedN("InstSimplify");
  for (auto &func : funcs) {
    for (auto &bb : func.bbs) {
      for (size_t instr_id = 0; instr_id < bb.instrs.size(); instr_id++) {
        if (simplify(bb.instrs[instr_id], bb.instrs, instr_id)) {
          instr_id--;
          continue;
        }
        if (multi_simplify(bb.instrs, instr_id)) {
          instr_id--;
          continue;
        }
      }
    }
  }
}

void InstSimplify::early_apply(FVec<MFunc> &funcs) {
  ZoneScopedN("InstSimplifyEarly");
  for (auto &func : funcs) {
    for (auto &bb : func.bbs) {
      for (size_t instr_id = 0; instr_id < bb.instrs.size(); instr_id++) {
        if (early_simplify(bb.instrs[instr_id], bb.instrs, instr_id)) {
          instr_id--;
          continue;
        }
        if (early_multi_simplify(bb.instrs, instr_id)) {
          instr_id--;
          continue;
        }
      }
    }
  }
}

} // namespace foptim::fmir
