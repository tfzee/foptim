#include <fmt/color.h>
#include <fmt/core.h>

#include "instr.hpp"
#include "mir/func.hpp"
namespace foptim::fmir {

#define ReturnString(sop, name) \
  case sop::name:               \
    return #name

const char *getNameFromOpcode(GOpcode code, u32 sop) {
  switch (code) {
    case GOpcode::GBase:
      switch ((GBaseSubtype)sop) {
        ReturnString(GBaseSubtype, INVALID);
        ReturnString(GBaseSubtype, mov);
        ReturnString(GBaseSubtype, push);
        ReturnString(GBaseSubtype, pop);
        ReturnString(GBaseSubtype, call);
        ReturnString(GBaseSubtype, ret);
        ReturnString(GBaseSubtype, arg_setup);
        ReturnString(GBaseSubtype, invoke);
      }
    case GOpcode::GJmp:
      switch ((GJumpSubtype)sop) {
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
      switch ((GConvSubtype)sop) {
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
      switch ((GArithSubtype)sop) {
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
      switch ((GCMovSubtype)sop) {
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
      switch ((GVecSubtype)sop) {
        ReturnString(GVecSubtype, INVALID);
        ReturnString(GVecSubtype, vadd);
        ReturnString(GVecSubtype, vsub);
        ReturnString(GVecSubtype, fmul);
        ReturnString(GVecSubtype, fdiv);
        ReturnString(GVecSubtype, ffmadd);
        ReturnString(GVecSubtype, fxor);
        ReturnString(GVecSubtype, fAnd);
        ReturnString(GVecSubtype, fOr);
        ReturnString(GVecSubtype, fShl);
        ReturnString(GVecSubtype, fMax);
        ReturnString(GVecSubtype, fMin);
      }
    case GOpcode::X86:
      switch ((X86Subtype)sop) {
        ReturnString(X86Subtype, INVALID);
        ReturnString(X86Subtype, lea);
        ReturnString(X86Subtype, LockXAdd2);
        ReturnString(X86Subtype, vextractf128);
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
        ReturnString(X86Subtype, lzcnt);
        ReturnString(X86Subtype, vpextr);
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
      switch ((GJumpSubtype)instr.sop) {
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
      switch ((GConvSubtype)instr.sop) {
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
      switch ((GCMovSubtype)instr.sop) {
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
      switch ((GArithSubtype)instr.sop) {
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
      switch ((GVecSubtype)instr.sop) {
        case GVecSubtype::INVALID:
          return;
        case GVecSubtype::vadd:
        case GVecSubtype::fMax:
        case GVecSubtype::fMin:
        case GVecSubtype::vsub:
        case GVecSubtype::fmul:
        case GVecSubtype::fdiv:
        case GVecSubtype::ffmadd:
        case GVecSubtype::fxor:
        case GVecSubtype::fAnd:
        case GVecSubtype::fOr:
        case GVecSubtype::fShl:
          out.push_back({0, instr.args[0]});
          return;
      }
    case GOpcode::X86:
      switch ((X86Subtype)instr.sop) {
        case X86Subtype::INVALID:
          return;
        case X86Subtype::lea:
        case X86Subtype::vextractf128:
        case X86Subtype::vextractf64x4:
        case X86Subtype::vextractf64x2:
        case X86Subtype::vpermil:
        case X86Subtype::sqrt:
        case X86Subtype::HAdd:
        case X86Subtype::movlhps:
        case X86Subtype::movhlps:
        case X86Subtype::vpshuf:
        case X86Subtype::vpextr:
        case X86Subtype::punpckl:
        case X86Subtype::vbroadcast:
        case X86Subtype::lzcnt:
        case X86Subtype::vmovshdup:
        case X86Subtype::vround:
        case X86Subtype::ffmadd132:
        case X86Subtype::ffmadd213:
        case X86Subtype::ffmadd231:
        case X86Subtype::vgatherq:
        case X86Subtype::vpcmpeq:
          out.push_back({0, instr.args[0]});
          return;
        case X86Subtype::LockXAdd2:
          out.push_back({0, instr.args[0]});
          out.push_back({1, instr.args[1]});
          return;
      }
    case GOpcode::GBase:
      switch ((GBaseSubtype)instr.sop) {
        case GBaseSubtype::INVALID:
          return;
        case GBaseSubtype::mov:
        case GBaseSubtype::pop:
          out.push_back({0, instr.args[0]});
          return;
        case GBaseSubtype::call:
        case GBaseSubtype::push:
        case GBaseSubtype::ret:
        case GBaseSubtype::arg_setup:
          return;
        case GBaseSubtype::invoke:
          if (instr.n_args > 1) {
            out.push_back({1, instr.args[1]});
            if (instr.n_args > 2) {
              out.push_back({2, instr.args[2]});
            }
          }
          return;
      }
  }
}

// TODO: could also return len and take a Margument[4] as arg
void read_args(const MInstr &instr, TVec<ArgData> &out) {
  switch (instr.bop) {
    case GOpcode::GBase:
      switch ((GBaseSubtype)instr.sop) {
        case GBaseSubtype::INVALID:
          return;
        case GBaseSubtype::mov:
          out.push_back({1, instr.args[1]});
          return;
        case GBaseSubtype::push:
        case GBaseSubtype::call:
        case GBaseSubtype::invoke:
        case GBaseSubtype::arg_setup:
          out.push_back({0, instr.args[0]});
          return;
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
      switch ((GJumpSubtype)instr.sop) {
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
      switch ((GConvSubtype)instr.sop) {
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
      switch ((GArithSubtype)instr.sop) {
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
      switch ((GCMovSubtype)instr.sop) {
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
      switch ((GVecSubtype)instr.sop) {
        case GVecSubtype::INVALID:
          return;
        case GVecSubtype::fxor:
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
        case GVecSubtype::fmul:
        case GVecSubtype::fdiv:
        case GVecSubtype::fAnd:
        case GVecSubtype::fOr:
        case GVecSubtype::fShl:
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
      switch ((X86Subtype)instr.sop) {
        case X86Subtype::INVALID:
          return;
        case X86Subtype::lea:
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
        case X86Subtype::vextractf128:
        case X86Subtype::punpckl:
        case X86Subtype::vbroadcast:
        case X86Subtype::vpermil:
        case X86Subtype::vround:
        case X86Subtype::LockXAdd2:
          out.push_back({1, instr.args[1]});
          out.push_back({2, instr.args[2]});
          return;
        case X86Subtype::ffmadd132:
        case X86Subtype::vpextr:
        case X86Subtype::ffmadd213:
        case X86Subtype::ffmadd231:
        case X86Subtype::vgatherq:
        case X86Subtype::vpcmpeq:
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
      return sop == (u32)GBaseSubtype::ret;
    case GOpcode::GConv:
    case GOpcode::GArith:
    case GOpcode::GVec:
    case GOpcode::GCMov:
    case GOpcode::X86:
      return false;
    case GOpcode::GJmp:
      switch ((GJumpSubtype)sop) {
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
  if (instr.is(GCMovSubtype::cmov)) {
    if (instr.n_args != 3) {
      fmt::println("Cmov should have 3 args {}", instr);
      return false;
    }
  }
  return true;
}
bool verify(const MBB &bb) {
  auto back_instr = bb.instrs.back();
  if (!back_instr.is(GBaseSubtype::ret) && !back_instr.is(GJumpSubtype::jmp)) {
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

}  // namespace
bool verify(const MFunc &func) {
  for (const auto &bb : func.bbs) {
    if (!verify(bb)) {
      fmt::println("In bb {}", bb);
      return false;
    }
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

}  // namespace foptim::fmir

fmt::appender fmt::formatter<foptim::fmir::MFunc>::format(
    foptim::fmir::MFunc const &func, format_context &ctx) const {
  auto app = ctx.out();
  if (color) {
    app = fmt::format_to(app, "func {} (",
                         fmt::styled(func.name.c_str(), color_func));
  } else {
    app = fmt::format_to(app, "func {} (", func.name.c_str());
  }
  auto n_args = func.args.size();
  for (foptim::u32 i = 0; i < n_args; i++) {
    if (color) {
      app = fmt::format_to(app, "{:c}: {:c}, ", func.args[i], func.args[i].ty);
    } else {
      app = fmt::format_to(app, "{}: {}, ", func.args[i], func.args[i].ty);
    }
  }
  app = fmt::format_to(app, ")\n");

  for (size_t bb_indx = 0; bb_indx < func.bbs.size(); bb_indx++) {
    if (color) {
      app = fmt::format_to(app, "  {}:\n{:c}", fmt::styled(bb_indx, color_bb),
                           func.bbs[bb_indx]);
    } else {
      app = fmt::format_to(app, "  {}:\n{}", bb_indx, func.bbs[bb_indx]);
    }
  }
  return app;
}

fmt::appender fmt::formatter<foptim::fmir::MBB>::format(
    foptim::fmir::MBB const &bb, format_context &ctx) const {
  auto app = ctx.out();
  for (const auto &instr : bb.instrs) {
    if (color) {
      app = fmt::format_to(app, "    {:c}\n", instr);
    } else {
      app = fmt::format_to(app, "    {}\n", instr);
    }
  }
  return app;
}

fmt::appender fmt::formatter<foptim::fmir::MInstr>::format(
    foptim::fmir::MInstr const &v, format_context &ctx) const {
  auto app = ctx.out();
  if (v.is(foptim::fmir::GBaseSubtype::mov)) {
    return fmt::format_to(app, "{:c} = {:c}", v.args[0], v.args[1]);
  }
  if (v.is(foptim::fmir::GArithSubtype::add2)) {
    return fmt::format_to(app, "{:c} += {:c}", v.args[0], v.args[1]);
  }
  if (v.is(foptim::fmir::GArithSubtype::sub2)) {
    return fmt::format_to(app, "{:c} -= {:c}", v.args[0], v.args[1]);
  }
  if (v.is(foptim::fmir::GArithSubtype::mul2)) {
    return fmt::format_to(app, "{:c} *= {:c}", v.args[0], v.args[1]);
  }
  if (v.is(foptim::fmir::GArithSubtype::lxor2)) {
    if (v.args[0] == v.args[1]) {
      return fmt::format_to(app, "clear {:c}", v.args[0]);
    }
    return fmt::format_to(app, "{:c} ^= {:c}", v.args[0], v.args[1]);
  }
  if (v.is(foptim::fmir::GVecSubtype::fxor)) {
    if (v.args[0] == v.args[1] && v.args[0] == v.args[2]) {
      return fmt::format_to(app, "clear {:c}", v.args[0]);
    }
    return fmt::format_to(app, "{:c} = {:cd} ^ {:c}", v.args[0], v.args[1],
                          v.args[2]);
  }
  if (v.is(foptim::fmir::GVecSubtype::vsub)) {
    return fmt::format_to(app, "{:c} = {:c} - {:c}", v.args[0], v.args[1],
                          v.args[2]);
  }
  if (v.is(foptim::fmir::GVecSubtype::vadd)) {
    return fmt::format_to(app, "{:c} = {:c} + {:c}", v.args[0], v.args[1],
                          v.args[2]);
  }
  if (v.is(foptim::fmir::GVecSubtype::fmul)) {
    return fmt::format_to(app, "{:c} = {:c} * {:c}", v.args[0], v.args[1],
                          v.args[2]);
  }
  if (v.is(foptim::fmir::GVecSubtype::fdiv)) {
    return fmt::format_to(app, "{:c} = {:c} / {:c}", v.args[0], v.args[1],
                          v.args[2]);
  }
  app = fmt::format_to(app, "{}(", getNameFromOpcode(v.bop, v.sop));
  for (size_t arg_indx = 0; arg_indx < v.n_args; arg_indx++) {
    if (color) {
      app = fmt::format_to(app, "{:c}, ", v.args[arg_indx]);
    } else {
      app = fmt::format_to(app, "{}, ", v.args[arg_indx]);
    }
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
  if (!color) {
    switch (value.type) {
      case foptim::fmir::MArgument::ArgumentType::MemLabel:
        return fmt::format_to(app, "[{}]: {}",
                              fmt::styled(value.label, color_func), value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemImmLabel:
        return fmt::format_to(app, "[{} + {}]: {}",
                              fmt::styled(value.label, color_func), value.imm,
                              value.ty);
      case foptim::fmir::MArgument::ArgumentType::Label:
        return fmt::format_to(app, "{}", fmt::styled(value.label, color_func));
      case foptim::fmir::MArgument::ArgumentType::Imm: {
        if (value.ty == foptim::fmir::Type::Float32) {
          return fmt::format_to(
              app, color_number, "{}f",
              std::bit_cast<foptim::f32>(
                  (foptim::u32)std::bit_cast<foptim::u64>(value.immf)));
        }
        if (value.ty == foptim::fmir::Type::Float64) {
          return fmt::format_to(app, color_number, "{}d", value.immf);
        }
        return fmt::format_to(app, color_number, "{}:{}",
                              (foptim::i64)value.imm, value.ty);
      }
      case foptim::fmir::MArgument::ArgumentType::VReg:
        return fmt::format_to(app, "{}:{}", value.reg, value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemVReg:
        return fmt::format_to(app, "[{}]:{}", value.reg, value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemVRegVReg:
        return fmt::format_to(app, "[{} + {}]:{}", value.reg, value.indx,
                              value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemImm:
        return fmt::format_to(app, "[{}]:{}", (foptim::i64)value.imm, value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemImmVReg:
        return fmt::format_to(app, "[{} + {}]:{}", value.reg,
                              (foptim::i64)value.imm, value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemImmVRegVReg:
        return fmt::format_to(app, "[{} + {} + {}]:{}", value.reg, value.indx,
                              (foptim::i64)value.imm, value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemImmVRegScale:
        return fmt::format_to(app, "[{}*{} + {}]:{}", value.indx,
                              1 << value.scale, (foptim::i64)value.imm,
                              value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemVRegVRegScale:
        return fmt::format_to(app, "[{} + {}*{}]:{}", value.reg, value.indx,
                              1 << value.scale, value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemImmVRegVRegScale:
        return fmt::format_to(app, "[{} + {}*{} + {}]:{}", value.reg,
                              value.indx, 1 << value.scale,
                              (foptim::i64)value.imm, value.ty);
    }
  } else {
    switch (value.type) {
      case foptim::fmir::MArgument::ArgumentType::MemLabel:
        return fmt::format_to(app, "[{}]: {:c}",
                              fmt::styled(value.label, color_func), value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemImmLabel:
        return fmt::format_to(app, "[{} + {}]: {:c}",
                              fmt::styled(value.label, color_func),
                              (foptim::i64)value.imm, value.ty);
      case foptim::fmir::MArgument::ArgumentType::Label:
        return fmt::format_to(app, "{}", fmt::styled(value.label, color_func));
      case foptim::fmir::MArgument::ArgumentType::Imm: {
        if (value.ty == foptim::fmir::Type::Float32) {
          return fmt::format_to(
              app, color_number, "{}f",
              std::bit_cast<foptim::f32>(
                  (foptim::u32)std::bit_cast<foptim::u64>(value.immf)));
        }
        if (value.ty == foptim::fmir::Type::Float64) {
          return fmt::format_to(app, color_number, "{}d", value.immf);
        }
        return fmt::format_to(app, color_number, "{}:{:c}",
                              (foptim::i64)value.imm, value.ty);
      }
      case foptim::fmir::MArgument::ArgumentType::VReg:
        return fmt::format_to(app, "{:c}:{:c}", value.reg, value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemVReg:
        return fmt::format_to(app, "[{:c}]:{:c}", value.reg, value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemVRegVReg:
        return fmt::format_to(app, "[{:c} + {:c}]:{:c}", value.reg, value.indx,
                              value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemImm:
        return fmt::format_to(app, "[{}]:{:c}", (foptim::i64)value.imm,
                              value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemImmVReg:
        return fmt::format_to(app, "[{:c} + {}]:{:c}", value.reg,
                              (foptim::i64)value.imm, value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemImmVRegVReg:
        return fmt::format_to(app, "[{:c} + {:c} + {}]:{:c}", value.reg,
                              value.indx, (foptim::i64)value.imm, value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemImmVRegScale:
        return fmt::format_to(app, "[{:c}*{} + {}]:{:c}", value.indx,
                              1 << value.scale, (foptim::i64)value.imm,
                              value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemVRegVRegScale:
        return fmt::format_to(app, "[{:c} + {:c}*{}]:{:c}", value.reg,
                              value.indx, 1 << value.scale, value.ty);
      case foptim::fmir::MArgument::ArgumentType::MemImmVRegVRegScale:
        return fmt::format_to(app, "[{:c} + {:c}*{} + {}]:{:c}", value.reg,
                              value.indx, 1 << value.scale,
                              (foptim::i64)value.imm, value.ty);
    }
  }
}

fmt::appender fmt::formatter<foptim::fmir::Type>::format(
    foptim::fmir::Type const &v, format_context &ctx) const {
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
    case foptim::fmir::Type::Float64x4:
      return fmt::format_to(app, col, "f64x4");
    case foptim::fmir::Type::Float64x8:
      return fmt::format_to(app, col, "f64x8");
  }
}

fmt::appender fmt::formatter<foptim::fmir::VReg>::format(
    foptim::fmir::VReg const &value, format_context &ctx) const {
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

  if (get_size(value.ty) == 0) {
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
        return fmt::format_to(
            app, col_vec, "$mm{}",
            ((foptim::u8)value.c_reg() - (foptim::u8)CReg::mm0));
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
        return fmt::format_to(
            app, col_vec, "$mm{}",
            ((foptim::u8)value.c_reg() - (foptim::u8)CReg::mm0));
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
        return fmt::format_to(
            app, col_vec, "${}mm{}", size_name,
            ((foptim::u8)value.c_reg() - (foptim::u8)CReg::mm0));
      }
      default:
        TODO("URNEACH");
    }
  } else {
    TODO("unrach?");
  }
  return fmt::format_to(app, "REG PRINT FAIL TYPE:{} OFF:{}", value.ty,
                        (foptim::u8)value.c_reg());
}
