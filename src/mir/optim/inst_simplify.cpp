#include "inst_simplify.hpp"

#include <fmt/base.h>
#include <fmt/core.h>

#include <cmath>

#include "mir/instr.hpp"

namespace foptim::fmir {

namespace {
//@returns true if the instruction was deleted
bool simplify(MInstr &instr, FVec<MInstr> &instrs, size_t instr_id) {
  if ((instr.is(GConvSubtype::mov_zx) || instr.is(GConvSubtype::mov_sx)) &&
      instr.args[0].ty == instr.args[1].ty) {
    instr.bop = GOpcode::GBase;
    instr.sop = static_cast<u32>(GBaseSubtype::mov);
    return false;
  }
  if (instr.is(GBaseSubtype::mov) || instr.is(GConvSubtype::mov_zx)) {
    if (instr.args[0] == instr.args[1]) {
      instrs.erase(instrs.begin() + static_cast<i64>(instr_id));
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
        instr.sop = static_cast<u32>(GArithSubtype::lxor2);
        instr.n_args = 2;
        instr.args[1] = instr.args[0];
        return false;
      }
      if (is_zero && instr.args[0].reg.is_vec_reg()) {
        instr.bop = GOpcode::GVec;
        instr.sop = static_cast<u32>(GVecSubtype::vXor);
        instr.n_args = 3;
        instr.args[1] = instr.args[0];
        instr.args[2] = instr.args[0];
        return false;
      }
    }
  } else if (instr.is(GCMovSubtype::cmov)) {
    if (instr.args[0] == instr.args[2]) {
      instrs.erase(instrs.begin() + static_cast<i64>(instr_id));
      return true;
    }
    // if both inputs are the same replace iwth basic move
    if (instr.args[1] == instr.args[2]) {
      instr.bop = GOpcode::GBase;
      instr.sop = static_cast<u32>(GBaseSubtype::mov);
      instr.n_args = 2;
      return false;
    }
  }
  return false;
}
//@returns true if the instruction was deleted
bool early_simplify(MInstr &instr, FVec<MInstr> &instrs, size_t instr_id) {
  if (instr.is(GBaseSubtype::mov) || instr.is(GConvSubtype::mov_zx)) {
    if (instr.args[0] == instr.args[1] && instr.args[0].isReg() &&
        !instr.args[0].reg.is_concrete()) {
      instrs.erase(instrs.begin() + static_cast<i64>(instr_id));
      return true;
    }
  }
  // if we load from memory we can just change it into a mov
  if (instr.is(GConvSubtype::itrunc) || instr.is(GConvSubtype::mov_zx)) {
    if (instr.args[1].ty == instr.args[0].ty) {
      instr.bop = GOpcode::GBase;
      instr.sop = static_cast<u32>(GBaseSubtype::mov);
    } else if (instr.is(GConvSubtype::itrunc) && instr.args[1].isMem() &&
               instr.args[0].isReg() && !instr.args[0].reg.is_concrete()) {
      instr.bop = GOpcode::GBase;
      instr.sop = static_cast<u32>(GBaseSubtype::mov);
      instr.args[0].reg = instr.args[0].reg.retype(instr.args[1].ty);
    }
  }
  return false;
}

bool early_multi_simplify(FVec<MInstr> &instrs, size_t instr_id) {
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

class VRegCounts {
 public:
  explicit VRegCounts(const MFunc &func) : func_(func) {}

  u32 get(const VReg &reg) {
    if (!built_) {
      build();
    }
    return reg.virt_id() < counts_.size() ? counts_[reg.virt_id()] : 0;
  }

 private:
  void count(const VReg &reg) {
    if (reg.is_concrete()) {
      return;
    }
    if (reg.virt_id() >= counts_.size()) {
      counts_.resize(reg.virt_id() + 1, 0);
    }
    counts_[reg.virt_id()]++;
  }

  void build() {
    built_ = true;
    for (const auto &bb : func_.bbs) {
      for (const auto &instr : bb.instrs) {
        for (size_t i = 0; i < instr.n_args; i++) {
          const auto &arg = instr.args[i];
          switch (arg.type) {
            case MArgument::ArgumentType::VReg:
            case MArgument::ArgumentType::MemVReg:
            case MArgument::ArgumentType::MemImmVReg:
              count(arg.reg);
              break;
            case MArgument::ArgumentType::MemImmVRegScale:
            case MArgument::ArgumentType::MemLabelVreg:
            case MArgument::ArgumentType::MemLabelVregScale:
              count(arg.indx);
              break;
            case MArgument::ArgumentType::MemImmVRegVReg:
            case MArgument::ArgumentType::MemVRegVRegScale:
            case MArgument::ArgumentType::MemVRegVReg:
            case MArgument::ArgumentType::MemImmVRegVRegScale:
              count(arg.reg);
              count(arg.indx);
              break;
            default:
              break;
          }
        }
      }
    }
  }

  const MFunc &func_;
  TVec<u32> counts_;
  bool built_ = false;
};

// zero extending narrow load followed by a zero extension of its result
// fuses into one extending load
bool fuse_extending_load(FVec<MInstr> &instrs, size_t instr_id,
                         VRegCounts &counts) {
  if (instr_id + 1 >= instrs.size()) {
    return false;
  }
  auto &load = instrs[instr_id];
  auto &ext = instrs[instr_id + 1];
  if (!load.is(GConvSubtype::mov_zx) || !ext.is(GConvSubtype::mov_zx) ||
      !load.args[0].isReg() || !load.args[1].isMem() || !ext.args[0].isReg() ||
      !ext.args[1].isReg()) {
    return false;
  }
  const auto &loaded = load.args[0].reg;
  const auto &mem_ty = load.args[1].ty;
  if (loaded.is_concrete() || loaded.is_vec_reg() || loaded.ty != Type::Int32 ||
      (mem_ty != Type::Int8 && mem_ty != Type::Int16) ||
      !(ext.args[1].reg == loaded) || ext.args[1].ty != mem_ty ||
      ext.args[0].reg.is_vec_reg() || get_size(ext.args[0].ty) < 4) {
    return false;
  }
  // the load and the extension are the only appearances
  if (counts.get(loaded) != 2) {
    return false;
  }
  load.args[0] = ext.args[0];
  instrs.erase(instrs.begin() + static_cast<i64>(instr_id) + 1);
  return true;
}

bool multi_simplify(FVec<MInstr> &instrs, size_t instr_id) {
  if (instr_id + 1 < instrs.size() &&
      ((instrs[instr_id + 0].is(GArithSubtype::add2) &&
        instrs[instr_id + 1].is(GArithSubtype::sub2)) ||
       (instrs[instr_id + 0].is(GArithSubtype::sub2) &&
        instrs[instr_id + 1].is(GArithSubtype::add2)))) {
    auto a0 = instrs[instr_id + 0];
    auto a1 = instrs[instr_id + 1];
    if (a0.args[0] == a1.args[0] && a0.args[1] == a1.args[1]) {
      instrs.erase(instrs.begin() + static_cast<i64>(instr_id),
                   instrs.begin() + static_cast<i64>(instr_id) + 2);
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
  if (instr_id + 1 < instrs.size() &&
      instrs[instr_id + 0].is(X86Subtype::lea) &&
      instrs[instr_id + 1].is(GArithSubtype::add2)) {
    auto &lea = instrs[instr_id + 0];
    auto &add = instrs[instr_id + 1];
    // x = lea(a*b + c1)
    // x += c2
    // =>  x = lea(a*b + c1+c2)
    bool lea_can_use_imm =
        MArgument::ArgumentType::MemImm == lea.args[1].type ||
        MArgument::ArgumentType::MemImmVReg == lea.args[1].type ||
        MArgument::ArgumentType::MemImmVRegVReg == lea.args[1].type ||
        MArgument::ArgumentType::MemImmVRegScale == lea.args[1].type ||
        MArgument::ArgumentType::MemImmVRegVRegScale == lea.args[1].type ||
        MArgument::ArgumentType::MemImmLabel == lea.args[1].type;
    if (lea.args[0] == add.args[0] && add.args[1].isImm() && lea_can_use_imm) {
      assert(lea.args[1].isMem());
      u64 res_val = lea.args[1].imm + add.args[1].imm;
      if (std::in_range<std::uint32_t>(res_val)) {
        lea.args[1].imm = res_val;
        instrs.erase(instrs.begin() + static_cast<i64>(instr_id) + 1,
                     instrs.begin() + static_cast<i64>(instr_id) + 2);
        return true;
      }
    }
  }
  return false;
}
}  // namespace

void InstSimplifyImpl::impl_apply(MFunc &func) {
  for (auto &bb : func.bbs) {
    for (size_t instr_id = 0; instr_id < bb.instrs.size(); instr_id++) {
      if (simplify(bb.instrs[instr_id], bb.instrs, instr_id) ||
          early_simplify(bb.instrs[instr_id], bb.instrs, instr_id)) {
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

void InstSimplifyImpl::impl_early(MFunc &func) {
  VRegCounts counts{func};
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
      if (fuse_extending_load(bb.instrs, instr_id, counts)) {
        continue;
      }
    }
  }
}

}  // namespace foptim::fmir
