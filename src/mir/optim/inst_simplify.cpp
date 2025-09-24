#include <fmt/core.h>

#include <cmath>

#include "inst_simplify.hpp"
#include "mir/instr.hpp"

namespace foptim::fmir {

namespace {
//@returns true if the instruction was deleted
bool simplify(MInstr &instr, IRVec<MInstr> &instrs, size_t instr_id) {
  if (instr.is(GBaseSubtype::mov) || instr.is(GConvSubtype::mov_zx)) {
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
        instr.bop = GOpcode::GArith;
        instr.sop = (u32)GArithSubtype::lxor2;
        instr.n_args = 2;
        instr.args[1] = instr.args[0];
        return false;
      }
      if (is_zero && instr.args[0].reg.is_vec_reg()) {
        instr.bop = GOpcode::GVec;
        instr.sop = (u32)GVecSubtype::fxor;
        instr.n_args = 3;
        instr.args[1] = instr.args[0];
        instr.args[2] = instr.args[0];
        return false;
      }
    }
  } else if (instr.is(GCMovSubtype::cmov)) {
    if (instr.args[0] == instr.args[2]) {
      instrs.erase(instrs.begin() + instr_id);
      return true;
    }
    // if both inputs are the same replace iwth basic move
    if (instr.args[1] == instr.args[2]) {
      instr.bop = GOpcode::GBase;
      instr.sop = (u32)GBaseSubtype::mov;
      instr.n_args = 2;
      return false;
    }
  }
  return false;
}
//@returns true if the instruction was deleted
bool early_simplify(MInstr &instr, IRVec<MInstr> &instrs, size_t instr_id) {
  if (instr.is(GBaseSubtype::mov) || instr.is(GConvSubtype::mov_zx)) {
    if (instr.args[0] == instr.args[1] && instr.args[0].isReg() &&
        !instr.args[0].reg.is_concrete()) {
      instrs.erase(instrs.begin() + instr_id);
      return true;
    }
  }
  return false;
}

bool early_multi_simplify(IRVec<MInstr> &instrs, size_t instr_id) {
  if (instr_id + 2 < instrs.size() &&
      instrs[instr_id + 0].is(GBaseSubtype::mov) &&
      (instrs[instr_id + 1].is(GArithSubtype::add2) ||
       instrs[instr_id + 1].is(GArithSubtype::sub2)) &&
      instrs[instr_id + 2].is(GBaseSubtype::mov)) {
    auto &get_val = instrs[instr_id + 0];
    auto &op = instrs[instr_id + 1];
    auto &writeback = instrs[instr_id + 2];
    // x = y
    // x += _
    // y = x
    if (get_val.args[0] == op.args[0] && get_val.args[0] == writeback.args[1] &&
        get_val.args[1] == writeback.args[0] &&
        get_val.args[0] != get_val.args[1] && op.args[1].isImm()) {
      writeback.bop = op.bop;
      writeback.sop = op.sop;
      writeback.args[1] = op.args[1];

      // // cjmp x _
      // if (instr_id + 3 < instrs.size()) {
      //   auto &i4 = instrs[instr_id + 3];
      //   // TODO: expand
      //   if ((i4.is(GJumpSubtype::cjmp_int_eq) ||
      //        i4.is(GJumpSubtype::cjmp_int_slt) ||
      //        i4.is(GJumpSubtype::cjmp_int_ult) ||
      //        i4.is(GJumpSubtype::cjmp_int_sgt) ||
      //        i4.is(GJumpSubtype::cjmp_int_ugt) ||
      //        i4.is(GJumpSubtype::cjmp_int_ne) ||
      //        i4.is(GJumpSubtype::cjmp_int_sle) ||
      //        i4.is(GJumpSubtype::cjmp_int_ule) ||
      //        i4.is(GJumpSubtype::cjmp_int_sge) ||
      //        i4.is(GJumpSubtype::cjmp_int_uge)) &&
      //       i4.args[0] == i1.args[0]) {
      //     i4.args[0] = i1.args[1];
      //   }
      // }
    }
  }
  return false;
}

bool multi_simplify(IRVec<MInstr> &instrs, size_t instr_id) {
  if (instr_id + 1 < instrs.size() &&
      ((instrs[instr_id + 0].is(GArithSubtype::add2) &&
        instrs[instr_id + 1].is(GArithSubtype::sub2)) ||
       (instrs[instr_id + 0].is(GArithSubtype::sub2) &&
        instrs[instr_id + 1].is(GArithSubtype::add2)))) {
    auto a0 = instrs[instr_id + 0];
    auto a1 = instrs[instr_id + 1];
    if (a0.args[0] == a1.args[0] && a0.args[1] == a1.args[1]) {
      instrs.erase(instrs.begin() + instr_id, instrs.begin() + instr_id + 2);
      return true;
    }
  }
  if (instr_id + 2 < instrs.size() &&
      instrs[instr_id + 0].is(GBaseSubtype::mov) &&
      (instrs[instr_id + 1].is(GArithSubtype::add2) ||
       instrs[instr_id + 1].is(GArithSubtype::sub2)) &&
      instrs[instr_id + 2].is(GBaseSubtype::mov)) {
    auto &get_val = instrs[instr_id + 0];
    auto &op = instrs[instr_id + 1];
    auto &writeback = instrs[instr_id + 2];
    // x = y
    // x += _
    // y = x
    if (get_val.args[0] == op.args[0] && get_val.args[0] == writeback.args[1] &&
        get_val.args[1] == writeback.args[0] &&
        get_val.args[0] != get_val.args[1] && op.args[1].isImm()) {
      writeback.bop = op.bop;
      writeback.sop = op.sop;
      writeback.args[1] = op.args[1];
    }
  }
  return false;
}
}  // namespace

void InstSimplify::apply(MFunc &func) {
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

void InstSimplify::apply(FVec<MFunc> &funcs) {
  ZoneScopedN("InstSimplify");
  for (auto &func : funcs) {
    apply(func);
  }
}

void InstSimplify::early_apply(MFunc &func) {
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
void InstSimplify::early_apply(FVec<MFunc> &funcs) {
  ZoneScopedN("InstSimplifyEarly");
  for (auto &func : funcs) {
    early_apply(func);
  }
}

}  // namespace foptim::fmir
