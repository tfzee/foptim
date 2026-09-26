#include "instr.hpp"

#include <fmt/base.h>
#include <fmt/color.h>
#include <fmt/format.h>

#include "mir/analysis/live_variables.hpp"
#include "mir/func.hpp"
#include "mir/global.hpp"
#include "utils/logging.hpp"
#include "utils/set.hpp"
namespace foptim::fmir {

#define ReturnString(sop, name)                                                \
  case sop::name:                                                              \
    return #name

const char *getNameFromOpcode(GOpcode code, u32 sop) {
  switch (code) {
  case GOpcode::GBase:
    switch (static_cast<GBaseSubtype>(sop)) {
      ReturnString(GBaseSubtype, INVALID);
      ReturnString(GBaseSubtype, unreach);
      ReturnString(GBaseSubtype, mov);
      ReturnString(GBaseSubtype, push);
      ReturnString(GBaseSubtype, pop);
      ReturnString(GBaseSubtype, stack_arg_load);
      ReturnString(GBaseSubtype, call);
      ReturnString(GBaseSubtype, ret);
      ReturnString(GBaseSubtype, arg_setup);
      ReturnString(GBaseSubtype, ret_setup);
      ReturnString(GBaseSubtype, invoke);
    }
  case GOpcode::GJmp:
    switch (static_cast<GJumpSubtype>(sop)) {
      ReturnString(GJumpSubtype, INVALID);
      ReturnString(GJumpSubtype, icmp_slt);
      ReturnString(GJumpSubtype, icmp_eq);
      ReturnString(GJumpSubtype, icmp_ult);
      ReturnString(GJumpSubtype, icmp_ne);
      ReturnString(GJumpSubtype, icmp_sgt);
      ReturnString(GJumpSubtype, icmp_ugt);
      ReturnString(GJumpSubtype, icmp_uge);
      ReturnString(GJumpSubtype, icmp_ule);
      ReturnString(GJumpSubtype, icmp_sge);
      ReturnString(GJumpSubtype, icmp_sle);
      ReturnString(GJumpSubtype, icmp_mul_overflow);
      ReturnString(GJumpSubtype, icmp_add_overflow);
      ReturnString(GJumpSubtype, fcmp_isNaN);
      ReturnString(GJumpSubtype, fcmp_oeq);
      ReturnString(GJumpSubtype, fcmp_ogt);
      ReturnString(GJumpSubtype, fcmp_oge);
      ReturnString(GJumpSubtype, fcmp_olt);
      ReturnString(GJumpSubtype, fcmp_ole);
      ReturnString(GJumpSubtype, fcmp_one);
      ReturnString(GJumpSubtype, fcmp_ord);
      ReturnString(GJumpSubtype, fcmp_uno);
      ReturnString(GJumpSubtype, fcmp_ueq);
      ReturnString(GJumpSubtype, fcmp_ugt);
      ReturnString(GJumpSubtype, fcmp_uge);
      ReturnString(GJumpSubtype, fcmp_ult);
      ReturnString(GJumpSubtype, fcmp_ule);
      ReturnString(GJumpSubtype, fcmp_une);
      ReturnString(GJumpSubtype, cjmp_and);
      ReturnString(GJumpSubtype, cjmp_or);
      ReturnString(GJumpSubtype, cjmp_int_slt);
      ReturnString(GJumpSubtype, cjmp_int_sge);
      ReturnString(GJumpSubtype, cjmp_int_sle);
      ReturnString(GJumpSubtype, cjmp_int_sgt);
      ReturnString(GJumpSubtype, cjmp_int_ult);
      ReturnString(GJumpSubtype, cjmp_int_ule);
      ReturnString(GJumpSubtype, cjmp_int_ugt);
      ReturnString(GJumpSubtype, cjmp_int_uge);
      ReturnString(GJumpSubtype, cjmp_int_ne);
      ReturnString(GJumpSubtype, cjmp_int_eq);
      ReturnString(GJumpSubtype, cjmp_flt_oeq);
      ReturnString(GJumpSubtype, cjmp_flt_ogt);
      ReturnString(GJumpSubtype, cjmp_flt_oge);
      ReturnString(GJumpSubtype, cjmp_flt_olt);
      ReturnString(GJumpSubtype, cjmp_flt_ole);
      ReturnString(GJumpSubtype, cjmp_flt_one);
      ReturnString(GJumpSubtype, cjmp_flt_ord);
      ReturnString(GJumpSubtype, cjmp_flt_uno);
      ReturnString(GJumpSubtype, cjmp_flt_ueq);
      ReturnString(GJumpSubtype, cjmp_flt_ugt);
      ReturnString(GJumpSubtype, cjmp_flt_uge);
      ReturnString(GJumpSubtype, cjmp_flt_ult);
      ReturnString(GJumpSubtype, cjmp_flt_ule);
      ReturnString(GJumpSubtype, cjmp_flt_une);
      ReturnString(GJumpSubtype, cjmp);
      ReturnString(GJumpSubtype, jmp);
    }
  case GOpcode::GConv:
    switch (static_cast<GConvSubtype>(sop)) {
      ReturnString(GConvSubtype, INVALID);
      ReturnString(GConvSubtype, SI2FL);
      ReturnString(GConvSubtype, UI2FL);
      ReturnString(GConvSubtype, FL2SI);
      ReturnString(GConvSubtype, FL2UI);
      ReturnString(GConvSubtype, F64_ext);
      ReturnString(GConvSubtype, F32_trunc);
      ReturnString(GConvSubtype, itrunc);
      ReturnString(GConvSubtype, mov_zx);
      ReturnString(GConvSubtype, mov_sx);
    }
  case GOpcode::GArith:
    switch (static_cast<GArithSubtype>(sop)) {
      ReturnString(GArithSubtype, INVALID);
      ReturnString(GArithSubtype, abs);
      ReturnString(GArithSubtype, shl2);
      ReturnString(GArithSubtype, shr2);
      ReturnString(GArithSubtype, sar2);
      ReturnString(GArithSubtype, land2);
      ReturnString(GArithSubtype, lor2);
      ReturnString(GArithSubtype, lxor2);
      ReturnString(GArithSubtype, add2);
      ReturnString(GArithSubtype, sub2);
      ReturnString(GArithSubtype, mul2);
      ReturnString(GArithSubtype, not1);
      ReturnString(GArithSubtype, neg1);
      ReturnString(GArithSubtype, idiv);
      ReturnString(GArithSubtype, udiv);
      ReturnString(GArithSubtype, smul3);
    }
  case GOpcode::GCMov:
    switch (static_cast<GCMovSubtype>(sop)) {
      ReturnString(GCMovSubtype, INVALID);
      ReturnString(GCMovSubtype, cmov);
      ReturnString(GCMovSubtype, cmov_ns);
      ReturnString(GCMovSubtype, cmov_sgt);
      ReturnString(GCMovSubtype, cmov_slt);
      ReturnString(GCMovSubtype, cmov_ult);
      ReturnString(GCMovSubtype, cmov_sge);
      ReturnString(GCMovSubtype, cmov_sle);
      ReturnString(GCMovSubtype, cmov_ne);
      ReturnString(GCMovSubtype, cmov_eq);
      ReturnString(GCMovSubtype, cmov_ugt);
      ReturnString(GCMovSubtype, cmov_uge);
      ReturnString(GCMovSubtype, cmov_ule);
    }
  case GOpcode::GVec:
    switch (static_cast<GVecSubtype>(sop)) {
      ReturnString(GVecSubtype, INVALID);
      ReturnString(GVecSubtype, vadd);
      ReturnString(GVecSubtype, vsub);
      ReturnString(GVecSubtype, vmul);
      ReturnString(GVecSubtype, vdiv);
      ReturnString(GVecSubtype, ffmadd);
      ReturnString(GVecSubtype, vXor);
      ReturnString(GVecSubtype, vAnd);
      ReturnString(GVecSubtype, vOr);
      ReturnString(GVecSubtype, vShl);
      ReturnString(GVecSubtype, fMax);
      ReturnString(GVecSubtype, fMin);
    }
  case GOpcode::X86:
    switch (static_cast<X86Subtype>(sop)) {
      ReturnString(X86Subtype, INVALID);
      ReturnString(X86Subtype, vblendv);
      ReturnString(X86Subtype, vcmp);
      ReturnString(X86Subtype, psrl);
      ReturnString(X86Subtype, psll);
      ReturnString(X86Subtype, pmuludq);
      ReturnString(X86Subtype, padd);
      ReturnString(X86Subtype, lea);
      ReturnString(X86Subtype, LockXAdd2);
      ReturnString(X86Subtype, vextract128);
      ReturnString(X86Subtype, vinsert128);
      ReturnString(X86Subtype, vinsertps);
      ReturnString(X86Subtype, vextractf64x2);
      ReturnString(X86Subtype, vextractf64x4);
      ReturnString(X86Subtype, movlhps);
      ReturnString(X86Subtype, movhlps);
      ReturnString(X86Subtype, vpermil);
      ReturnString(X86Subtype, sqrt);
      ReturnString(X86Subtype, vmovshdup);
      ReturnString(X86Subtype, HAdd);
      ReturnString(X86Subtype, vpshuf);
      ReturnString(X86Subtype, punpckl);
      ReturnString(X86Subtype, vbroadcast);
      ReturnString(X86Subtype, tzcnt);
      ReturnString(X86Subtype, lzcnt);
      ReturnString(X86Subtype, popcnt);
      ReturnString(X86Subtype, vpextr);
      ReturnString(X86Subtype, vpinsr);
      ReturnString(X86Subtype, ffmadd132);
      ReturnString(X86Subtype, ffmadd213);
      ReturnString(X86Subtype, ffmadd231);
      ReturnString(X86Subtype, vgatherq);
      ReturnString(X86Subtype, vpcmpeq);
      ReturnString(X86Subtype, vround);
    }
  }
}
#undef ReturnString

void written_args(const MInstr &instr, TVec<ArgData> &out) {
  switch (instr.bop) {
  case GOpcode::GJmp:
    switch (static_cast<GJumpSubtype>(instr.sop)) {
    case GJumpSubtype::icmp_slt:
    case GJumpSubtype::icmp_eq:
    case GJumpSubtype::icmp_ult:
    case GJumpSubtype::icmp_ne:
    case GJumpSubtype::icmp_sgt:
    case GJumpSubtype::icmp_ugt:
    case GJumpSubtype::icmp_uge:
    case GJumpSubtype::icmp_ule:
    case GJumpSubtype::icmp_sge:
    case GJumpSubtype::icmp_sle:
    case GJumpSubtype::icmp_mul_overflow:
    case GJumpSubtype::icmp_add_overflow:
    case GJumpSubtype::fcmp_isNaN:
    case GJumpSubtype::fcmp_oeq:
    case GJumpSubtype::fcmp_ogt:
    case GJumpSubtype::fcmp_oge:
    case GJumpSubtype::fcmp_olt:
    case GJumpSubtype::fcmp_ole:
    case GJumpSubtype::fcmp_one:
    case GJumpSubtype::fcmp_ord:
    case GJumpSubtype::fcmp_uno:
    case GJumpSubtype::fcmp_ueq:
    case GJumpSubtype::fcmp_ugt:
    case GJumpSubtype::fcmp_uge:
    case GJumpSubtype::fcmp_ult:
    case GJumpSubtype::fcmp_ule:
    case GJumpSubtype::fcmp_une:
      out.push_back({0, instr.args[0]});
      return;
    case GJumpSubtype::INVALID:
    case GJumpSubtype::cjmp_int_slt:
    case GJumpSubtype::cjmp_int_sge:
    case GJumpSubtype::cjmp_int_sle:
    case GJumpSubtype::cjmp_int_sgt:
    case GJumpSubtype::cjmp_int_ult:
    case GJumpSubtype::cjmp_int_ule:
    case GJumpSubtype::cjmp_int_ugt:
    case GJumpSubtype::cjmp_int_uge:
    case GJumpSubtype::cjmp_int_ne:
    case GJumpSubtype::cjmp_int_eq:
    case GJumpSubtype::cjmp_flt_oeq:
    case GJumpSubtype::cjmp_flt_ogt:
    case GJumpSubtype::cjmp_flt_oge:
    case GJumpSubtype::cjmp_flt_olt:
    case GJumpSubtype::cjmp_flt_ole:
    case GJumpSubtype::cjmp_flt_one:
    case GJumpSubtype::cjmp_flt_ord:
    case GJumpSubtype::cjmp_flt_uno:
    case GJumpSubtype::cjmp_flt_ueq:
    case GJumpSubtype::cjmp_flt_ugt:
    case GJumpSubtype::cjmp_flt_uge:
    case GJumpSubtype::cjmp_flt_ult:
    case GJumpSubtype::cjmp_flt_ule:
    case GJumpSubtype::cjmp_flt_une:
    case GJumpSubtype::cjmp_and:
    case GJumpSubtype::cjmp_or:
    case GJumpSubtype::cjmp:
    case GJumpSubtype::jmp:
      return;
    }
  case GOpcode::GConv:
    switch (static_cast<GConvSubtype>(instr.sop)) {
    case GConvSubtype::SI2FL:
    case GConvSubtype::UI2FL:
    case GConvSubtype::FL2SI:
    case GConvSubtype::FL2UI:
    case GConvSubtype::F64_ext:
    case GConvSubtype::F32_trunc:
    case GConvSubtype::itrunc:
    case GConvSubtype::mov_zx:
    case GConvSubtype::mov_sx:
      out.push_back({0, instr.args[0]});
      return;
    case GConvSubtype::INVALID:
      return;
    }
  case GOpcode::GCMov:
    switch (static_cast<GCMovSubtype>(instr.sop)) {
    case GCMovSubtype::INVALID:
      return;
    case GCMovSubtype::cmov:
    case GCMovSubtype::cmov_ns:
    case GCMovSubtype::cmov_sgt:
    case GCMovSubtype::cmov_slt:
    case GCMovSubtype::cmov_ult:
    case GCMovSubtype::cmov_sge:
    case GCMovSubtype::cmov_sle:
    case GCMovSubtype::cmov_ne:
    case GCMovSubtype::cmov_eq:
    case GCMovSubtype::cmov_ugt:
    case GCMovSubtype::cmov_uge:
    case GCMovSubtype::cmov_ule:
      out.push_back({0, instr.args[0]});
      return;
    }
  case GOpcode::GArith:
    switch (static_cast<GArithSubtype>(instr.sop)) {
    case GArithSubtype::INVALID:
      return;
    case GArithSubtype::abs:
    case GArithSubtype::shl2:
    case GArithSubtype::shr2:
    case GArithSubtype::sar2:
    case GArithSubtype::land2:
    case GArithSubtype::lor2:
    case GArithSubtype::lxor2:
    case GArithSubtype::add2:
    case GArithSubtype::sub2:
    case GArithSubtype::mul2:
    case GArithSubtype::not1:
    case GArithSubtype::neg1:
    case GArithSubtype::smul3:
      out.push_back({0, instr.args[0]});
      return;
    case GArithSubtype::idiv:
    case GArithSubtype::udiv:
      out.push_back({0, instr.args[0]});
      out.push_back({1, instr.args[1]});
      return;
    }
  case GOpcode::GVec:
    switch (static_cast<GVecSubtype>(instr.sop)) {
    case GVecSubtype::INVALID:
      return;
    case GVecSubtype::vadd:
    case GVecSubtype::fMax:
    case GVecSubtype::fMin:
    case GVecSubtype::vsub:
    case GVecSubtype::vmul:
    case GVecSubtype::vdiv:
    case GVecSubtype::ffmadd:
    case GVecSubtype::vXor:
    case GVecSubtype::vAnd:
    case GVecSubtype::vOr:
    case GVecSubtype::vShl:
      out.push_back({0, instr.args[0]});
      return;
    }
  case GOpcode::X86:
    switch (static_cast<X86Subtype>(instr.sop)) {
    case X86Subtype::INVALID:
      return;
    case X86Subtype::lea:
    case X86Subtype::vextract128:
    case X86Subtype::vinsert128:
    case X86Subtype::vinsertps:
    case X86Subtype::vextractf64x4:
    case X86Subtype::vextractf64x2:
    case X86Subtype::vpermil:
    case X86Subtype::sqrt:
    case X86Subtype::HAdd:
    case X86Subtype::movlhps:
    case X86Subtype::movhlps:
    case X86Subtype::vpshuf:
    case X86Subtype::vpextr:
    case X86Subtype::vpinsr:
    case X86Subtype::punpckl:
    case X86Subtype::vbroadcast:
    case X86Subtype::popcnt:
    case X86Subtype::tzcnt:
    case X86Subtype::lzcnt:
    case X86Subtype::vmovshdup:
    case X86Subtype::vround:
    case X86Subtype::vblendv:
    case X86Subtype::vcmp:
    case X86Subtype::ffmadd132:
    case X86Subtype::ffmadd213:
    case X86Subtype::ffmadd231:
    case X86Subtype::vgatherq:
    case X86Subtype::vpcmpeq:
    case X86Subtype::psrl:
    case X86Subtype::psll:
    case X86Subtype::pmuludq:
    case X86Subtype::padd:
      out.push_back({0, instr.args[0]});
      return;
    case X86Subtype::LockXAdd2:
      out.push_back({0, instr.args[0]});
      out.push_back({1, instr.args[1]});
      return;
    }
  case GOpcode::GBase:
    switch (static_cast<GBaseSubtype>(instr.sop)) {
    case GBaseSubtype::INVALID:
    case GBaseSubtype::unreach:
      return;
    case GBaseSubtype::mov:
    case GBaseSubtype::stack_arg_load:
    case GBaseSubtype::pop:
      out.push_back({0, instr.args[0]});
      return;
    case GBaseSubtype::call:
    case GBaseSubtype::push:
    case GBaseSubtype::ret:
    case GBaseSubtype::arg_setup:
      return;
    case GBaseSubtype::ret_setup:
      out.push_back({0, instr.args[0]});
      return;
    case GBaseSubtype::invoke:
      // if (instr.n_args > 1) {
      //   out.push_back({1, instr.args[1]});
      //   if (instr.n_args > 2) {
      //     out.push_back({2, instr.args[2]});
      //   }
      // }
      return;
    }
  }
}

// TODO: could also return len and take a Margument[4] as arg
void read_args(const MInstr &instr, TVec<ArgData> &out) {
  switch (instr.bop) {
  case GOpcode::GBase:
    switch (static_cast<GBaseSubtype>(instr.sop)) {
    case GBaseSubtype::INVALID:
    case GBaseSubtype::unreach:
      return;
    case GBaseSubtype::stack_arg_load:
    case GBaseSubtype::mov:
      out.push_back({1, instr.args[1]});
      return;
    case GBaseSubtype::push:
    case GBaseSubtype::call:
    case GBaseSubtype::invoke:
    case GBaseSubtype::arg_setup:
      out.push_back({0, instr.args[0]});
      return;
    case GBaseSubtype::ret_setup:
    case GBaseSubtype::pop:
      return;
    case GBaseSubtype::ret:
      if (instr.n_args > 0) {
        out.push_back({0, instr.args[0]});
        if (instr.n_args > 1) {
          out.push_back({1, instr.args[1]});
        }
      }
      return;
    }
  case GOpcode::GJmp:
    switch (static_cast<GJumpSubtype>(instr.sop)) {
    case GJumpSubtype::INVALID:
    case GJumpSubtype::jmp:
      return;
    case GJumpSubtype::cjmp:
      out.push_back({0, instr.args[0]});
      return;
    case GJumpSubtype::icmp_slt:
    case GJumpSubtype::icmp_eq:
    case GJumpSubtype::icmp_ult:
    case GJumpSubtype::icmp_ne:
    case GJumpSubtype::icmp_sgt:
    case GJumpSubtype::icmp_ugt:
    case GJumpSubtype::icmp_uge:
    case GJumpSubtype::icmp_ule:
    case GJumpSubtype::icmp_sge:
    case GJumpSubtype::icmp_sle:
    case GJumpSubtype::icmp_mul_overflow:
    case GJumpSubtype::icmp_add_overflow:
    case GJumpSubtype::fcmp_isNaN:
    case GJumpSubtype::fcmp_oeq:
    case GJumpSubtype::fcmp_ogt:
    case GJumpSubtype::fcmp_oge:
    case GJumpSubtype::fcmp_olt:
    case GJumpSubtype::fcmp_ole:
    case GJumpSubtype::fcmp_one:
    case GJumpSubtype::fcmp_ord:
    case GJumpSubtype::fcmp_uno:
    case GJumpSubtype::fcmp_ueq:
    case GJumpSubtype::fcmp_ugt:
    case GJumpSubtype::fcmp_uge:
    case GJumpSubtype::fcmp_ult:
    case GJumpSubtype::fcmp_ule:
    case GJumpSubtype::fcmp_une:
      out.push_back({1, instr.args[1]});
      out.push_back({2, instr.args[2]});
      return;
    case GJumpSubtype::cjmp_int_slt:
    case GJumpSubtype::cjmp_int_sge:
    case GJumpSubtype::cjmp_int_sle:
    case GJumpSubtype::cjmp_int_sgt:
    case GJumpSubtype::cjmp_int_ult:
    case GJumpSubtype::cjmp_int_ule:
    case GJumpSubtype::cjmp_int_ugt:
    case GJumpSubtype::cjmp_int_uge:
    case GJumpSubtype::cjmp_int_ne:
    case GJumpSubtype::cjmp_int_eq:
    case GJumpSubtype::cjmp_flt_oeq:
    case GJumpSubtype::cjmp_flt_ogt:
    case GJumpSubtype::cjmp_flt_oge:
    case GJumpSubtype::cjmp_flt_olt:
    case GJumpSubtype::cjmp_flt_ole:
    case GJumpSubtype::cjmp_flt_one:
    case GJumpSubtype::cjmp_flt_ord:
    case GJumpSubtype::cjmp_flt_uno:
    case GJumpSubtype::cjmp_flt_ueq:
    case GJumpSubtype::cjmp_flt_ugt:
    case GJumpSubtype::cjmp_flt_uge:
    case GJumpSubtype::cjmp_flt_ult:
    case GJumpSubtype::cjmp_flt_ule:
    case GJumpSubtype::cjmp_flt_une:
    case GJumpSubtype::cjmp_and:
    case GJumpSubtype::cjmp_or:
      out.push_back({0, instr.args[0]});
      out.push_back({1, instr.args[1]});
      return;
    }
  case GOpcode::GConv:
    switch (static_cast<GConvSubtype>(instr.sop)) {
    case GConvSubtype::INVALID:
      return;
    case GConvSubtype::SI2FL:
    case GConvSubtype::UI2FL:
    case GConvSubtype::FL2SI:
    case GConvSubtype::FL2UI:
    case GConvSubtype::F64_ext:
    case GConvSubtype::F32_trunc:
    case GConvSubtype::itrunc:
    case GConvSubtype::mov_zx:
    case GConvSubtype::mov_sx:
      out.push_back({1, instr.args[1]});
      return;
    }
  case GOpcode::GArith:
    switch (static_cast<GArithSubtype>(instr.sop)) {
    case GArithSubtype::INVALID:
      return;
    case GArithSubtype::abs:
      out.push_back({1, instr.args[1]});
      return;
    case GArithSubtype::not1:
    case GArithSubtype::neg1:
      out.push_back({0, instr.args[0]});
      return;
    case GArithSubtype::lor2:
      if (instr.args[0] == instr.args[1]) {
        return;
      }
      out.push_back({0, instr.args[0]});
      out.push_back({1, instr.args[1]});
      return;
    case GArithSubtype::shl2:
    case GArithSubtype::shr2:
    case GArithSubtype::sar2:
    case GArithSubtype::land2:
    case GArithSubtype::lxor2:
    case GArithSubtype::add2:
    case GArithSubtype::sub2:
    case GArithSubtype::mul2:
      out.push_back({0, instr.args[0]});
      out.push_back({1, instr.args[1]});
      return;
    case GArithSubtype::smul3:
      out.push_back({1, instr.args[1]});
      out.push_back({2, instr.args[2]});
      return;
    case GArithSubtype::idiv:
    case GArithSubtype::udiv:
      out.push_back({2, instr.args[2]});
      out.push_back({3, instr.args[3]});
      return;
    }
  case GOpcode::GCMov:
    switch (static_cast<GCMovSubtype>(instr.sop)) {
    case GCMovSubtype::INVALID:
      return;
    case GCMovSubtype::cmov:
      out.push_back({0, instr.args[0]});
      out.push_back({1, instr.args[1]});
      out.push_back({2, instr.args[2]});
      return;
    case GCMovSubtype::cmov_ns:
    case GCMovSubtype::cmov_sgt:
    case GCMovSubtype::cmov_slt:
    case GCMovSubtype::cmov_ult:
    case GCMovSubtype::cmov_sge:
    case GCMovSubtype::cmov_sle:
    case GCMovSubtype::cmov_ne:
    case GCMovSubtype::cmov_eq:
    case GCMovSubtype::cmov_ugt:
    case GCMovSubtype::cmov_uge:
    case GCMovSubtype::cmov_ule:
      out.push_back({0, instr.args[0]});
      out.push_back({1, instr.args[1]});
      out.push_back({2, instr.args[2]});
      out.push_back({3, instr.args[3]});
      return;
    }
  case GOpcode::GVec:
    switch (static_cast<GVecSubtype>(instr.sop)) {
    case GVecSubtype::INVALID:
      return;
    case GVecSubtype::vXor:
      if (instr.args[1] == instr.args[2]) {
        return;
      }
      out.push_back({1, instr.args[1]});
      out.push_back({2, instr.args[2]});
      return;
    case GVecSubtype::vadd:
    case GVecSubtype::vsub:
    case GVecSubtype::fMax:
    case GVecSubtype::fMin:
    case GVecSubtype::vmul:
    case GVecSubtype::vdiv:
    case GVecSubtype::vAnd:
    case GVecSubtype::vOr:
    case GVecSubtype::vShl:
      out.push_back({1, instr.args[1]});
      out.push_back({2, instr.args[2]});
      return;
    case GVecSubtype::ffmadd:
      out.push_back({0, instr.args[0]});
      out.push_back({1, instr.args[1]});
      out.push_back({2, instr.args[2]});
      return;
    }
  case GOpcode::X86:
    switch (static_cast<X86Subtype>(instr.sop)) {
    case X86Subtype::INVALID:
      return;
    case X86Subtype::lea:
    case X86Subtype::popcnt:
    case X86Subtype::tzcnt:
    case X86Subtype::lzcnt:
    case X86Subtype::vmovshdup:
    case X86Subtype::sqrt:
    case X86Subtype::HAdd:
      out.push_back({1, instr.args[1]});
      return;
    case X86Subtype::movlhps:
    case X86Subtype::movhlps:
    case X86Subtype::vpshuf:
    case X86Subtype::vextractf64x2:
    case X86Subtype::vextractf64x4:
    case X86Subtype::vextract128:
    case X86Subtype::vinsert128:
    case X86Subtype::vinsertps:
    case X86Subtype::punpckl:
    case X86Subtype::vbroadcast:
    case X86Subtype::vpermil:
    case X86Subtype::vround:
    case X86Subtype::LockXAdd2:
    case X86Subtype::psrl:
    case X86Subtype::psll:
    case X86Subtype::pmuludq:
    case X86Subtype::padd:
    case X86Subtype::vpcmpeq:
    case X86Subtype::vpextr:
    case X86Subtype::vpinsr:
      out.push_back({1, instr.args[1]});
      out.push_back({2, instr.args[2]});
      return;
    case X86Subtype::vblendv:
    case X86Subtype::vcmp:
      out.push_back({1, instr.args[1]});
      out.push_back({2, instr.args[2]});
      out.push_back({3, instr.args[3]});
      return;
    case X86Subtype::ffmadd132:
    case X86Subtype::ffmadd213:
    case X86Subtype::ffmadd231:
    case X86Subtype::vgatherq:
      out.push_back({0, instr.args[0]});
      out.push_back({1, instr.args[1]});
      out.push_back({2, instr.args[2]});
      return;
    }
    break;
  }
}

bool MInstr::is_control_flow(GOpcode c, u32 sop) {
  switch (c) {
  case GOpcode::GBase:
    return sop == static_cast<u32>(GBaseSubtype::ret);
  case GOpcode::GConv:
  case GOpcode::GArith:
  case GOpcode::GVec:
  case GOpcode::GCMov:
  case GOpcode::X86:
    return false;
  case GOpcode::GJmp:
    switch (static_cast<GJumpSubtype>(sop)) {
    case GJumpSubtype::cjmp_int_slt:
    case GJumpSubtype::cjmp_int_sge:
    case GJumpSubtype::cjmp_int_sle:
    case GJumpSubtype::cjmp_int_sgt:
    case GJumpSubtype::cjmp_int_ult:
    case GJumpSubtype::cjmp_int_ule:
    case GJumpSubtype::cjmp_int_ugt:
    case GJumpSubtype::cjmp_int_uge:
    case GJumpSubtype::cjmp_int_ne:
    case GJumpSubtype::cjmp_int_eq:
    case GJumpSubtype::cjmp_flt_oeq:
    case GJumpSubtype::cjmp_flt_ogt:
    case GJumpSubtype::cjmp_flt_oge:
    case GJumpSubtype::cjmp_flt_olt:
    case GJumpSubtype::cjmp_flt_ole:
    case GJumpSubtype::cjmp_flt_one:
    case GJumpSubtype::cjmp_flt_ord:
    case GJumpSubtype::cjmp_flt_uno:
    case GJumpSubtype::cjmp_flt_ueq:
    case GJumpSubtype::cjmp_flt_ugt:
    case GJumpSubtype::cjmp_flt_uge:
    case GJumpSubtype::cjmp_flt_ult:
    case GJumpSubtype::cjmp_flt_ule:
    case GJumpSubtype::cjmp_flt_une:
    case GJumpSubtype::cjmp_and:
    case GJumpSubtype::cjmp_or:
    case GJumpSubtype::cjmp:
    case GJumpSubtype::jmp:
      return true;
    default:
      return false;
    }
  }
}

namespace {
bool verify(const MInstr &instr) {
  if (instr.is(X86Subtype::lea)) {
    for (size_t arg_id = 0; arg_id < instr.n_args; arg_id++) {
      const auto &arg = instr.args[arg_id];
      if (arg.isMem()) {
        switch (arg.type) {
        case MArgument::ArgumentType::Imm:
        case MArgument::ArgumentType::VReg:
        case MArgument::ArgumentType::Label:
          UNREACH();
        case MArgument::ArgumentType::MemLabel:
        case MArgument::ArgumentType::MemImmLabel:
        case MArgument::ArgumentType::StackSlot:
        case MArgument::ArgumentType::MemImm:
          break;
        case MArgument::ArgumentType::MemImmVReg:
        case MArgument::ArgumentType::MemVReg:
          if (arg.reg.size() != get_size(arg.ty)) {
            fmt::println("Mem operand type doenst match mem type {} {}:{}",
                         instr, arg.reg, arg.reg.ty);
            return false;
          }
          break;
        case MArgument::ArgumentType::MemVRegVRegScale:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVRegScale:
          if (arg.reg.size() != get_size(arg.ty)) {
            fmt::println("Mem operand type doenst match mem type {} {}:{}",
                         instr, arg.reg, arg.reg.ty);
            return false;
          }
          if (arg.indx.size() != get_size(arg.ty)) {
            fmt::println("Mem operand type doenst match mem type {} {}:{}",
                         instr, arg.indx, arg.indx.ty);
            return false;
          }
          break;
        case MArgument::ArgumentType::MemLabelVregScale:
        case MArgument::ArgumentType::MemLabelVreg:
        case MArgument::ArgumentType::MemImmVRegScale:
          if (arg.indx.size() != get_size(arg.ty)) {
            fmt::println("Mem operand type doenst match mem type {} {}:{}",
                         instr, arg.indx, arg.indx.ty);
            return false;
          }
          break;
        }
      }
    }
  }
  if (instr.is(GCMovSubtype::cmov)) {
    if (instr.n_args != 3) {
      fmt::println("Cmov should have 3 args {}", instr);
      return false;
    }
  } else if (instr.is(GArithSubtype::smul3)) {
    if (instr.n_args != 3) {
      fmt::println("smul should have 3 args {}", instr);
      return false;
    }
    if (instr.args[1].isImm() && instr.args[2].isImm()) {
      fmt::println("smul cant have 2 immediates {}", instr);
      return false;
    }
  } else if (instr.is(X86Subtype::vpextr)) {
    if (instr.n_args != 3) {
      fmt::println("should have 3 args {}", instr);
      return false;
    }
    if (!instr.args[2].isImm()) {
      fmt::println("3rd arg needs to be imm {}", instr);
      return false;
    }
    if (!instr.args[1].is_vec_reg() || get_size(instr.args[1].ty) > 128) {
      fmt::println("2nd arg needs to be xmm vecreg {}", instr);
      return false;
    }
  }
  return true;
}

bool verify(const MBB &bb) {
  auto back_instr = bb.instrs.back();
  if (!back_instr.is(GBaseSubtype::unreach) &&
      !back_instr.is(GBaseSubtype::ret) && !back_instr.is(GJumpSubtype::jmp)) {
    fmt::println("Last instruction should be jmp or ret but is {}",
                 bb.instrs.back());
    return false;
  }
  for (const auto &instr : bb.instrs) {
    if (!verify(instr)) {
      fmt::println("In instr {}", instr);
      return false;
    }
  }

  return true;
}

} // namespace
bool verify(const MFunc &func) {
  for (const auto &bb : func.bbs) {
    if (!verify(bb)) {
      fmt::println("In bb {}", bb);
      return false;
    }
  }
  TSet<u64> writes;
  TSet<u64> reads;

  auto insertw = [&](auto arg) {
    if (!arg.is_concrete()) {
      writes.insert(reg_to_uid(arg));
    }
  };
  auto insertr = [&](auto arg) {
    if (!arg.is_concrete()) {
      reads.insert(reg_to_uid(arg));
    }
  };
  for (auto arg : func.args) {
    insertw(arg);
  }

  TVec<ArgData> args;
  for (const auto &bb : func.bbs) {
    for (const auto &instr : bb.instrs) {
      args.clear();
      written_args(instr, args);
      for (auto arg : args) {
        switch (arg.arg.type) {
        case MArgument::ArgumentType::Imm:
        case MArgument::ArgumentType::MemImm:
        case MArgument::ArgumentType::Label:
        case MArgument::ArgumentType::MemLabel:
        case MArgument::ArgumentType::MemImmLabel:
        case MArgument::ArgumentType::StackSlot:
          break;
        case MArgument::ArgumentType::VReg:
          insertw(arg.arg.reg);
          break;
        case MArgument::ArgumentType::MemVReg:
        case MArgument::ArgumentType::MemImmVReg:
          insertr(arg.arg.reg);
          break;
        case MArgument::ArgumentType::MemLabelVregScale:
        case MArgument::ArgumentType::MemLabelVreg:
        case MArgument::ArgumentType::MemImmVRegScale:
          insertr(arg.arg.indx);
          break;
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVRegScale:
        case MArgument::ArgumentType::MemImmVRegVRegScale:
          insertr(arg.arg.reg);
          insertr(arg.arg.indx);
          break;
        }
      }
      args.clear();
      read_args(instr, args);
      for (auto arg : args) {
        switch (arg.arg.type) {
        case MArgument::ArgumentType::Imm:
        case MArgument::ArgumentType::MemImm:
        case MArgument::ArgumentType::Label:
        case MArgument::ArgumentType::MemLabel:
        case MArgument::ArgumentType::StackSlot:
        case MArgument::ArgumentType::MemImmLabel:
          break;
        case MArgument::ArgumentType::VReg:
        case MArgument::ArgumentType::MemVReg:
        case MArgument::ArgumentType::MemImmVReg:
          insertr(arg.arg.reg);
          break;
        case MArgument::ArgumentType::MemLabelVregScale:
        case MArgument::ArgumentType::MemLabelVreg:
        case MArgument::ArgumentType::MemImmVRegScale:
          insertr(arg.arg.indx);
          break;
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVRegScale:
        case MArgument::ArgumentType::MemImmVRegVRegScale:
          insertr(arg.arg.reg);
          insertr(arg.arg.indx);
          break;
        }
      }
    }
  }
  bool any_missing = false;
  for (auto read : reads) {
    if (!writes.contains(read)) {
      fmt::println("Register {} was read but not written", uid_to_reg(read));
      any_missing = true;
    }
  }
  if (any_missing) {
    fmt::println("In func {}", func);
    return false;
  }
  return true;
}

bool verify(const FVec<MFunc> &funcs) {
  for (const auto &func : funcs) {
    if (!verify(func)) {
      fmt::println("In func {}", func);
      return false;
    }
  }
  return true;
}

} // namespace foptim::fmir

fmt::appender
fmt::formatter<foptim::fmir::Global>::format(foptim::fmir::Global const &glob,
                                             format_context &ctx) const {
  auto app = ctx.out();
  switch (glob.vis) {
  case foptim::fir::LinkVisibility::Default:
    fmt::println("DEFAULT");
    break;
  case foptim::fir::LinkVisibility::Hidden:
    fmt::println("HIDDEN");
    break;
  case foptim::fir::LinkVisibility::Protected:
    fmt::println("PROTECTED");
    break;
  }
  if (glob.data.empty()) {
    fmt::println(" DECL");
  }
  fmt::println(" ALIGN({})", glob.min_align);
  fmt::println(" {}", glob.name);
  fmt::println(" = ({}) .....", glob.size);
  return app;
}

fmt::appender
fmt::formatter<foptim::fmir::MFunc>::format(foptim::fmir::MFunc const &func,
                                            format_context &ctx) const {
  auto app = ctx.out();
  if (color) {
    app = fmt::format_to(app, "func {} (",
                         fmt::styled(func.name.c_str(), color_func));
  } else {
    app = fmt::format_to(app, "func {} (", func.name.c_str());
  }
  auto n_args = func.args.size();
  fmt::formatter<foptim::fmir::VReg> vreg_fmt;
  forward_it(vreg_fmt);
  fmt::formatter<foptim::fmir::Type> ty_fmt;
  forward_it(ty_fmt);
  for (foptim::u32 i = 0; i < n_args; i++) {
    app = vreg_fmt.format(func.args[i], ctx);
    app = fmt::format_to(app, ": ");
    app = ty_fmt.format(func.args[i].ty, ctx);
    app = fmt::format_to(app, ", ");
  }
  app = fmt::format_to(app, ")\n");

  fmt::formatter<foptim::fmir::MBB> bb_fmt;
  forward_it(bb_fmt);

  for (size_t bb_indx = 0; bb_indx < func.bbs.size(); bb_indx++) {
    if (color) {
      app = fmt::format_to(app, color_bb, "  {}:\n", bb_indx);
    } else {
      app = fmt::format_to(app, "  {}:\n", bb_indx);
    }
    app = bb_fmt.format(func.bbs[bb_indx], ctx);
  }
  return app;
}

fmt::appender
fmt::formatter<foptim::fmir::MBB>::format(foptim::fmir::MBB const &bb,
                                          format_context &ctx) const {
  auto app = ctx.out();
  fmt::formatter<foptim::fmir::MInstr> instr_fmt;
  forward_it(instr_fmt);
  for (const auto &instr : bb.instrs) {
    app = fmt::format_to(app, "    ");
    app = instr_fmt.format(instr, ctx);
    app = fmt::format_to(app, "\n");
  }
  return app;
}

fmt::appender
fmt::formatter<foptim::fmir::MInstr>::format(foptim::fmir::MInstr const &v,
                                             format_context &ctx) const {
  auto app = ctx.out();
  fmt::formatter<foptim::fmir::MArgument> arg_fmt;
  forward_it(arg_fmt);
  if (v.is(foptim::fmir::GBaseSubtype::mov)) {
    app = arg_fmt.format(v.args[0], ctx);
    app = fmt::format_to(app, " = ");
    return arg_fmt.format(v.args[1], ctx);
  }
  if (v.is(foptim::fmir::GArithSubtype::add2)) {
    app = arg_fmt.format(v.args[0], ctx);
    app = fmt::format_to(app, " += ");
    return arg_fmt.format(v.args[1], ctx);
  }
  if (v.is(foptim::fmir::GArithSubtype::sub2)) {
    app = arg_fmt.format(v.args[0], ctx);
    app = fmt::format_to(app, " -= ");
    return arg_fmt.format(v.args[1], ctx);
  }
  if (v.is(foptim::fmir::GArithSubtype::mul2)) {
    app = arg_fmt.format(v.args[0], ctx);
    app = fmt::format_to(app, " *= ");
    return arg_fmt.format(v.args[1], ctx);
  }
  if (v.is(foptim::fmir::GArithSubtype::lxor2)) {
    if (v.args[0] == v.args[1]) {
      app = fmt::format_to(app, "clear ");
      app = arg_fmt.format(v.args[0], ctx);
      return app;
    }
    app = arg_fmt.format(v.args[0], ctx);
    app = fmt::format_to(app, " ^= ");
    return arg_fmt.format(v.args[1], ctx);
  }
  if (v.is(foptim::fmir::GVecSubtype::vXor)) {
    if (v.args[0] == v.args[1] && v.args[0] == v.args[2]) {
      return fmt::format_to(app, "clear {:c}", v.args[0]);
    }
    app = arg_fmt.format(v.args[0], ctx);
    app = fmt::format_to(app, " = ");
    app = arg_fmt.format(v.args[1], ctx);
    app = fmt::format_to(app, " ^ ");
    return arg_fmt.format(v.args[2], ctx);
  }
  if (v.is(foptim::fmir::GVecSubtype::vsub)) {
    app = arg_fmt.format(v.args[0], ctx);
    app = fmt::format_to(app, " = ");
    app = arg_fmt.format(v.args[1], ctx);
    app = fmt::format_to(app, " - ");
    return arg_fmt.format(v.args[2], ctx);
  }
  if (v.is(foptim::fmir::GVecSubtype::vadd)) {
    app = arg_fmt.format(v.args[0], ctx);
    app = fmt::format_to(app, " = ");
    app = arg_fmt.format(v.args[1], ctx);
    app = fmt::format_to(app, " + ");
    return arg_fmt.format(v.args[2], ctx);
  }
  if (v.is(foptim::fmir::GVecSubtype::vmul)) {
    app = arg_fmt.format(v.args[0], ctx);
    app = fmt::format_to(app, " = ");
    app = arg_fmt.format(v.args[1], ctx);
    app = fmt::format_to(app, " * ");
    return arg_fmt.format(v.args[2], ctx);
  }
  if (v.is(foptim::fmir::GVecSubtype::vdiv)) {
    app = arg_fmt.format(v.args[0], ctx);
    app = fmt::format_to(app, " = ");
    app = arg_fmt.format(v.args[1], ctx);
    app = fmt::format_to(app, " / ");
    return arg_fmt.format(v.args[2], ctx);
  }
  app = fmt::format_to(app, "{}(", getNameFromOpcode(v.bop, v.sop));
  for (size_t arg_indx = 0; arg_indx < v.n_args; arg_indx++) {
    app = arg_fmt.format(v.args[arg_indx], ctx);
    app = fmt::format_to(app, ", ", v.args[arg_indx]);
  }
  app = fmt::format_to(app, ")");
  if ((v.is(foptim::fmir::GBaseSubtype::call) ||
       v.is(foptim::fmir::GBaseSubtype::invoke)) &&
      v.is_var_arg_call) {
    app = fmt::format_to(app, "  VARARG");
  }
  if (v.has_bb_ref) {
    if (color) {
      app = fmt::format_to(app, " -> {}", fmt::styled(v.bb_ref, color_bb));
    } else {
      app = fmt::format_to(app, " -> {}", v.bb_ref);
    }
  }
  return app;
}
fmt::appender fmt::formatter<foptim::fmir::MArgument>::format(
    foptim::fmir::MArgument const &value, format_context &ctx) const {
  auto app = ctx.out();
  fmt::formatter<foptim::fmir::Type> ty_fmt;
  forward_it(ty_fmt);
  fmt::formatter<foptim::fmir::VReg> vreg_fmt;
  forward_it(vreg_fmt);
  switch (value.type) {
  case foptim::fmir::MArgument::ArgumentType::StackSlot:
    app = fmt::format_to(app, "S<");
    app = fmt::format_to(app, color_func, "{}", value.imm);
    return fmt::format_to(app, ">@{}", value.scale);
  case foptim::fmir::MArgument::ArgumentType::MemLabel:
    app = fmt::format_to(app, "[{}]: ", fmt::styled(value.label, color_func));
    return ty_fmt.format(value.ty, ctx);

  case foptim::fmir::MArgument::ArgumentType::MemImmLabel:
    app =
        fmt::format_to(app, "[{} + {}]: ", fmt::styled(value.label, color_func),
                       static_cast<foptim::i64>(value.imm));
    return ty_fmt.format(value.ty, ctx);
  case foptim::fmir::MArgument::ArgumentType::Label:
    return fmt::format_to(app, "{}", fmt::styled(value.label, color_func));
  case foptim::fmir::MArgument::ArgumentType::Imm: {
    if (value.ty == foptim::fmir::Type::Float32) {
      return fmt::format_to(app, color_number, "{}f",
                            std::bit_cast<foptim::f32>(static_cast<foptim::u32>(
                                std::bit_cast<foptim::u64>(value.immf))));
    }
    if (value.ty == foptim::fmir::Type::Float64) {
      return fmt::format_to(app, color_number, "{}d", value.immf);
    }
    app = fmt::format_to(app, color_number,
                         "{}: ", static_cast<foptim::i64>(value.imm));
    return ty_fmt.format(value.ty, ctx);
  }
  case foptim::fmir::MArgument::ArgumentType::VReg:
    app = vreg_fmt.format(value.reg, ctx);
    app = fmt::format_to(app, ": ");
    return ty_fmt.format(value.ty, ctx);

  case foptim::fmir::MArgument::ArgumentType::MemVReg:
    app = fmt::format_to(app, "[");
    app = vreg_fmt.format(value.reg, ctx);
    app = fmt::format_to(app, "]: ");
    return ty_fmt.format(value.ty, ctx);

  case foptim::fmir::MArgument::ArgumentType::MemVRegVReg:
    app = fmt::format_to(app, "[");
    app = vreg_fmt.format(value.reg, ctx);
    app = fmt::format_to(app, " + ");
    app = vreg_fmt.format(value.indx, ctx);
    app = fmt::format_to(app, "]: ");
    return ty_fmt.format(value.ty, ctx);

  case foptim::fmir::MArgument::ArgumentType::MemImm:
    app = fmt::format_to(app, "[{}]: ", static_cast<foptim::i64>(value.imm));
    return ty_fmt.format(value.ty, ctx);

  case foptim::fmir::MArgument::ArgumentType::MemImmVReg:
    app = fmt::format_to(app, "[");
    app = vreg_fmt.format(value.reg, ctx);
    app = fmt::format_to(app, " + {}]: ", static_cast<foptim::i64>(value.imm));
    return ty_fmt.format(value.ty, ctx);

  case foptim::fmir::MArgument::ArgumentType::MemImmVRegVReg:
    app = fmt::format_to(app, "[");
    app = vreg_fmt.format(value.reg, ctx);
    app = fmt::format_to(app, " + ");
    app = vreg_fmt.format(value.indx, ctx);
    app = fmt::format_to(app, " + {}]: ", static_cast<foptim::i64>(value.imm));
    return ty_fmt.format(value.ty, ctx);

  case foptim::fmir::MArgument::ArgumentType::MemLabelVreg:
    app = fmt::format_to(app, "[");
    app = vreg_fmt.format(value.indx, ctx);
    app = fmt::format_to(app, " + {}]: ", value.label);
    return ty_fmt.format(value.ty, ctx);

  case foptim::fmir::MArgument::ArgumentType::MemLabelVregScale:
    app = fmt::format_to(app, "[");
    app = vreg_fmt.format(value.indx, ctx);
    app = fmt::format_to(app, "*{} + {}]: ", 1 << value.scale, value.label);
    return ty_fmt.format(value.ty, ctx);

  case foptim::fmir::MArgument::ArgumentType::MemImmVRegScale:
    app = fmt::format_to(app, "[");
    app = vreg_fmt.format(value.indx, ctx);
    app = fmt::format_to(app, "*{} + {}]: ", 1 << value.scale,
                         static_cast<foptim::i64>(value.imm));
    return ty_fmt.format(value.ty, ctx);

  case foptim::fmir::MArgument::ArgumentType::MemVRegVRegScale:
    app = fmt::format_to(app, "[");
    app = vreg_fmt.format(value.reg, ctx);
    app = fmt::format_to(app, " + ");
    app = vreg_fmt.format(value.indx, ctx);
    app = fmt::format_to(app, " *{}]: ", 1 << value.scale);
    return ty_fmt.format(value.ty, ctx);

  case foptim::fmir::MArgument::ArgumentType::MemImmVRegVRegScale:
    app = fmt::format_to(app, "[");
    app = vreg_fmt.format(value.reg, ctx);
    app = fmt::format_to(app, " + ");
    app = vreg_fmt.format(value.indx, ctx);
    app = fmt::format_to(app, "*{} + {}]: ", 1 << value.scale,
                         static_cast<foptim::i64>(value.imm));
    return ty_fmt.format(value.ty, ctx);
  }
}

fmt::appender
fmt::formatter<foptim::fmir::Type>::format(foptim::fmir::Type const &v,
                                           format_context &ctx) const {
  auto app = ctx.out();
  const auto col = color ? fg(fmt::color::light_coral) : text_style{};
  switch (v) {
  case foptim::fmir::Type::INVALID:
    return fmt::format_to(app, col, "INVALID");
  case foptim::fmir::Type::Int8:
    return fmt::format_to(app, col, "i8");
  case foptim::fmir::Type::Int16:
    return fmt::format_to(app, col, "i16");
  case foptim::fmir::Type::Int32:
    return fmt::format_to(app, col, "i32");
  case foptim::fmir::Type::Int64:
    return fmt::format_to(app, col, "i64");
  case foptim::fmir::Type::Float32:
    return fmt::format_to(app, col, "f32");
  case foptim::fmir::Type::Float64:
    return fmt::format_to(app, col, "f64");
  case foptim::fmir::Type::Int32x4:
    return fmt::format_to(app, col, "i32x4");
  case foptim::fmir::Type::Int64x2:
    return fmt::format_to(app, col, "i64x2");
  case foptim::fmir::Type::Float32x2:
    return fmt::format_to(app, col, "f32x2");
  case foptim::fmir::Type::Float32x4:
    return fmt::format_to(app, col, "f32x4");
  case foptim::fmir::Type::Float64x2:
    return fmt::format_to(app, col, "f64x2");
  case foptim::fmir::Type::Int32x8:
    return fmt::format_to(app, col, "i32x8");
  case foptim::fmir::Type::Int64x4:
    return fmt::format_to(app, col, "i64x4");
  case foptim::fmir::Type::Float32x8:
    return fmt::format_to(app, col, "f32x8");
  case foptim::fmir::Type::Float32x16:
    return fmt::format_to(app, col, "f32x16");
  case foptim::fmir::Type::Int32x16:
    return fmt::format_to(app, col, "i32x16");
  case foptim::fmir::Type::Float64x4:
    return fmt::format_to(app, col, "f64x4");
  case foptim::fmir::Type::Float64x8:
    return fmt::format_to(app, col, "f64x8");
  case foptim::fmir::Type::Int64x8:
    return fmt::format_to(app, col, "i64x8");
  }
}

fmt::appender
fmt::formatter<foptim::fmir::VReg>::format(foptim::fmir::VReg const &value,
                                           format_context &ctx) const {
  using foptim::fmir::CReg;
  auto app = ctx.out();
  auto col_vec = color ? fg(fmt::color::steel_blue) : text_style{};
  auto colv2 = color ? color_value2 : text_style{};
  if (!value.is_concrete()) {
    if (color) {
      return fmt::format_to(app, color_value, "${}", value.virt_id());
    }
    return fmt::format_to(app, "${}", value.virt_id());
  }

  if (value.ty == foptim::fmir::Type::INVALID || get_size(value.ty) == 0) {
    switch (value.c_reg()) {
    case CReg::A:
      return fmt::format_to(app, colv2, "$a");
    case CReg::SP:
      return fmt::format_to(app, colv2, "$sp");
    case CReg::B:
      return fmt::format_to(app, colv2, "$b");
    case CReg::C:
      return fmt::format_to(app, colv2, "$c");
    case CReg::D:
      return fmt::format_to(app, colv2, "$d");
    case CReg::DI:
      return fmt::format_to(app, colv2, "$di");
    case CReg::SI:
      return fmt::format_to(app, colv2, "$si");
    case CReg::BP:
      return fmt::format_to(app, colv2, "$bp");
    case CReg::R8:
      return fmt::format_to(app, colv2, "$8");
    case CReg::R9:
      return fmt::format_to(app, colv2, "$9");
    case CReg::R10:
      return fmt::format_to(app, colv2, "$10");
    case CReg::R11:
      return fmt::format_to(app, colv2, "$11");
    case CReg::R12:
      return fmt::format_to(app, colv2, "$12");
    case CReg::R13:
      return fmt::format_to(app, colv2, "$13");
    case CReg::R14:
      return fmt::format_to(app, colv2, "$14");
    case CReg::R15:
      return fmt::format_to(app, colv2, "$15");
    default:
    }
  } else if (get_size(value.ty) == 1) {
    switch (value.c_reg()) {
    case CReg::A:
      return fmt::format_to(app, colv2, "$al");
    case CReg::SP:
      return fmt::format_to(app, colv2, "$spl");
    case CReg::B:
      return fmt::format_to(app, colv2, "$bl");
    case CReg::C:
      return fmt::format_to(app, colv2, "$cl");
    case CReg::D:
      return fmt::format_to(app, colv2, "$dl");
    case CReg::DI:
      return fmt::format_to(app, colv2, "$ldi");
    case CReg::SI:
      return fmt::format_to(app, colv2, "$lsi");
    case CReg::BP:
      return fmt::format_to(app, colv2, "$lbp");
    case CReg::R8:
      return fmt::format_to(app, colv2, "$r8l");
    case CReg::R9:
      return fmt::format_to(app, colv2, "$r9l");
    case CReg::R10:
      return fmt::format_to(app, colv2, "$r10l");
    case CReg::R11:
      return fmt::format_to(app, colv2, "$r11l");
    case CReg::R12:
      return fmt::format_to(app, colv2, "$r12l");
    case CReg::R13:
      return fmt::format_to(app, colv2, "$r13l");
    case CReg::R14:
      return fmt::format_to(app, colv2, "$r14l");
    case CReg::R15:
      return fmt::format_to(app, colv2, "$r15l");
    default:
    }
  } else if (get_size(value.ty) == 2) {
    switch (value.c_reg()) {
    case CReg::A:
      return fmt::format_to(app, colv2, "$ax");
    case CReg::SP:
      return fmt::format_to(app, colv2, "$sp");
    case CReg::B:
      return fmt::format_to(app, colv2, "$bx");
    case CReg::C:
      return fmt::format_to(app, colv2, "$cx");
    case CReg::D:
      return fmt::format_to(app, colv2, "$dx");
    case CReg::DI:
      return fmt::format_to(app, colv2, "$di");
    case CReg::SI:
      return fmt::format_to(app, colv2, "$si");
    case CReg::BP:
      return fmt::format_to(app, colv2, "$bp");
    case CReg::R8:
      return fmt::format_to(app, colv2, "$r8");
    case CReg::R9:
      return fmt::format_to(app, colv2, "$r9");
    case CReg::R10:
      return fmt::format_to(app, colv2, "$r10");
    case CReg::R11:
      return fmt::format_to(app, colv2, "$r11");
    case CReg::R12:
      return fmt::format_to(app, colv2, "$r12");
    case CReg::R13:
      return fmt::format_to(app, colv2, "$r13");
    case CReg::R14:
      return fmt::format_to(app, colv2, "$r14");
    case CReg::R15:
      return fmt::format_to(app, colv2, "$r15");
    default:
    }
  } else if (get_size(value.ty) == 4) {
    switch (value.c_reg()) {
    case CReg::A:
      return fmt::format_to(app, colv2, "$eax");
    case CReg::SP:
      return fmt::format_to(app, colv2, "$esp");
    case CReg::B:
      return fmt::format_to(app, colv2, "$ebx");
    case CReg::C:
      return fmt::format_to(app, colv2, "$ecx");
    case CReg::D:
      return fmt::format_to(app, colv2, "$edx");
    case CReg::DI:
      return fmt::format_to(app, colv2, "$edi");
    case CReg::SI:
      return fmt::format_to(app, colv2, "$esi");
    case CReg::BP:
      return fmt::format_to(app, colv2, "$ebp");
    case CReg::R8:
      return fmt::format_to(app, colv2, "$r8d");
    case CReg::R9:
      return fmt::format_to(app, colv2, "$r9d");
    case CReg::R10:
      return fmt::format_to(app, colv2, "$r10d");
    case CReg::R11:
      return fmt::format_to(app, colv2, "$r11d");
    case CReg::R12:
      return fmt::format_to(app, colv2, "$r12d");
    case CReg::R13:
      return fmt::format_to(app, colv2, "$r13d");
    case CReg::R14:
      return fmt::format_to(app, colv2, "$r14d");
    case CReg::R15:
      return fmt::format_to(app, colv2, "$r15d");
    case CReg::mm0:
    case CReg::mm1:
    case CReg::mm2:
    case CReg::mm3:
    case CReg::mm4:
    case CReg::mm5:
    case CReg::mm6:
    case CReg::mm7:
    case CReg::mm8:
    case CReg::mm9:
    case CReg::mm10:
    case CReg::mm11:
    case CReg::mm12:
    case CReg::mm13:
    case CReg::mm14:
    case CReg::mm15:
      return fmt::format_to(app, col_vec, "$mm{}",
                            (static_cast<foptim::u8>(value.c_reg()) -
                             static_cast<foptim::u8>(CReg::mm0)));
    default:
    }
  } else if (get_size(value.ty) == 8) {
    switch (value.c_reg()) {
    case CReg::A:
      return fmt::format_to(app, colv2, "$rax");
    case CReg::SP:
      return fmt::format_to(app, colv2, "$rsp");
    case CReg::B:
      return fmt::format_to(app, colv2, "$rbx");
    case CReg::C:
      return fmt::format_to(app, colv2, "$rcx");
    case CReg::D:
      return fmt::format_to(app, colv2, "$rdx");
    case CReg::DI:
      return fmt::format_to(app, colv2, "$rdi");
    case CReg::SI:
      return fmt::format_to(app, colv2, "$rsi");
    case CReg::BP:
      return fmt::format_to(app, colv2, "$rbp");
    case CReg::R8:
      return fmt::format_to(app, colv2, "$r8");
    case CReg::R9:
      return fmt::format_to(app, colv2, "$r9");
    case CReg::R10:
      return fmt::format_to(app, colv2, "$r10");
    case CReg::R11:
      return fmt::format_to(app, colv2, "$r11");
    case CReg::R12:
      return fmt::format_to(app, colv2, "$r12");
    case CReg::R13:
      return fmt::format_to(app, colv2, "$r13");
    case CReg::R14:
      return fmt::format_to(app, colv2, "$r14");
    case CReg::R15:
      return fmt::format_to(app, colv2, "$r15");
    case CReg::mm0:
    case CReg::mm1:
    case CReg::mm2:
    case CReg::mm3:
    case CReg::mm4:
    case CReg::mm5:
    case CReg::mm6:
    case CReg::mm7:
    case CReg::mm8:
    case CReg::mm9:
    case CReg::mm10:
    case CReg::mm11:
    case CReg::mm12:
    case CReg::mm13:
    case CReg::mm14:
    case CReg::mm15:
      return fmt::format_to(app, col_vec, "$mm{}",
                            (static_cast<foptim::u8>(value.c_reg()) -
                             static_cast<foptim::u8>(CReg::mm0)));
    default:
    }
  } else if (get_size(value.ty) > 8) {
    switch (value.c_reg()) {
    case CReg::mm0:
    case CReg::mm1:
    case CReg::mm2:
    case CReg::mm3:
    case CReg::mm4:
    case CReg::mm5:
    case CReg::mm6:
    case CReg::mm7:
    case CReg::mm8:
    case CReg::mm9:
    case CReg::mm10:
    case CReg::mm11:
    case CReg::mm12:
    case CReg::mm13:
    case CReg::mm14:
    case CReg::mm15: {
      auto size = get_size(value.ty);
      const auto *size_name = "?";
      switch (size) {
      case 16:
        size_name = "x";
        break;
      case 32:
        size_name = "y";
        break;
      case 64:
        size_name = "z";
        break;
      default:
        break;
      }
      return fmt::format_to(app, col_vec, "${}mm{}", size_name,
                            (static_cast<foptim::u8>(value.c_reg()) -
                             static_cast<foptim::u8>(CReg::mm0)));
    }
    default:
      TODO("URNEACH");
    }
  } else {
    TODO("unrach?");
  }
  return fmt::format_to(app, "REG PRINT FAIL TYPE:{} OFF:{}", value.ty,
                        static_cast<foptim::u8>(value.c_reg()));
}
