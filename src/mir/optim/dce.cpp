#include "dce.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"

namespace foptim::fmir {

namespace {
bool is_applicable(GOpcode op, u32 sop) {
  switch (op) {
    case GOpcode::GJmp:
      switch ((GJumpSubtype)sop) {
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
          return true;
        default:
          return false;
      }
    case GOpcode::GConv:
    case GOpcode::GArith:
    case GOpcode::GCMov:
    case GOpcode::GVec:
      return true;
    case GOpcode::GBase:
      switch ((GBaseSubtype)sop) {
        case GBaseSubtype::INVALID:
        case GBaseSubtype::mov:
          return true;
        case GBaseSubtype::push:
        case GBaseSubtype::pop:
        case GBaseSubtype::call:
        case GBaseSubtype::ret:
        case GBaseSubtype::arg_setup:
        case GBaseSubtype::invoke:
          return false;
      }
    case GOpcode::X86:
      switch ((X86Subtype)sop) {
        case X86Subtype::INVALID:
        case X86Subtype::sqrt:
        case X86Subtype::vpermil:
        case X86Subtype::movlhps:
        case X86Subtype::movhlps:
        case X86Subtype::vmovshdup:
        case X86Subtype::vpextr:
        case X86Subtype::HAdd:
        case X86Subtype::lea:
        case X86Subtype::vpshuf:
        case X86Subtype::punpckl:
        case X86Subtype::vbroadcast:
        case X86Subtype::lzcnt:
        case X86Subtype::ffmadd132:
        case X86Subtype::ffmadd213:
        case X86Subtype::ffmadd231:
        case X86Subtype::vgatherq:
        case X86Subtype::vpcmpeq:
          return true;
      }
  }
}
}  // namespace

void DeadCodeElim::apply_impl(MFunc &func) {
  CFG cfg{func};
  LiveVariables live{cfg, func};
  TVec<ArgData> w_args;
  TVec<ArgData> helper;
  helper.reserve(4);
  w_args.reserve(4);
  TVec<size_t> dead_instrs;

  for (size_t bb_id = 0; bb_id < func.bbs.size(); bb_id++) {
    auto &bb = func.bbs[bb_id];
    dead_instrs.clear();

    for (size_t instr_idp1 = bb.instrs.size(); instr_idp1 > 0; instr_idp1--) {
      const auto instr_id = instr_idp1 - 1;
      w_args.clear();
      written_args(bb.instrs[instr_id], w_args);

      if (!is_applicable(bb.instrs[instr_id].bop, bb.instrs[instr_id].sop)) {
        continue;
      }

      if (w_args.size() != 1 || !w_args[0].arg.isReg()) {
        continue;
      }

      const auto target_uid = reg_to_uid(w_args[0].arg.reg);

      // bool is_only_alive_in_this_bb = true;
      // for (size_t i = 0; i < func.bbs.size(); i++) {
      //   if (i != bb_id && live._live[i][target_uid]) {
      //     is_only_alive_in_this_bb = false;
      //   }
      // }
      // if (!is_only_alive_in_this_bb) {
      //   continue;
      // }
      // FIXME: This is a bit too pesimistic
      bool is_read =
          live._liveIn[bb_id][target_uid] || live._liveOut[bb_id][target_uid];

      auto next_use =
          find_next_use(bb.instrs, target_uid, instr_id + 1, helper);
      is_read |= next_use.is_read;

      if ((next_use.is_write && !next_use.is_read) || !is_read) {
        bb.instrs.erase(bb.instrs.begin() + instr_id);
        continue;
      }
    }
  }
}

void DeadCodeElim::apply(MFunc &func) { apply_impl(func); }
void DeadCodeElim::apply(FVec<MFunc> &funcs) {
  ZoneScopedNC("DCE", COLOR_OPTIMF);
  for (auto &func : funcs) {
    apply(func);
  }
}

}  // namespace foptim::fmir
