#include "dce.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"

namespace foptim::fmir {

bool is_applicable(Opcode op) {
  switch (op) {
  case Opcode::cmov:
  case Opcode::mov_zx:
  case Opcode::mov_sx:
  case Opcode::mov:
  case Opcode::itrunc:
  case Opcode::lea:
  case Opcode::shl2:
  case Opcode::shr2:
  case Opcode::sar2:
  case Opcode::land2:
  case Opcode::lor2:
  case Opcode::lxor2:
  case Opcode::add2:
  case Opcode::sub2:
  case Opcode::mul2:
  case Opcode::fadd:
  case Opcode::fsub:
  case Opcode::fmul:
  case Opcode::fdiv:
  case Opcode::ffmadd132:
  case Opcode::ffmadd213:
  case Opcode::ffmadd231:
  case Opcode::fxor:
  case Opcode::SI2FL:
  case Opcode::UI2FL:
  case Opcode::FL2SI:
  case Opcode::FL2UI:
  case Opcode::icmp_slt:
  case Opcode::icmp_eq:
  case Opcode::icmp_ult:
  case Opcode::icmp_ne:
  case Opcode::icmp_sgt:
  case Opcode::icmp_ugt:
  case Opcode::icmp_uge:
  case Opcode::icmp_ule:
  case Opcode::icmp_sge:
  case Opcode::icmp_sle:
  case Opcode::fcmp_oeq:
  case Opcode::fcmp_ogt:
  case Opcode::fcmp_oge:
  case Opcode::fcmp_olt:
  case Opcode::fcmp_ole:
  case Opcode::fcmp_one:
  case Opcode::fcmp_ord:
  case Opcode::fcmp_uno:
  case Opcode::fcmp_ueq:
  case Opcode::fcmp_ugt:
  case Opcode::fcmp_uge:
  case Opcode::fcmp_ult:
  case Opcode::fcmp_ule:
  case Opcode::fcmp_une:
    return true;
  default:
    return false;
  }
}

void DeadCodeElim::apply(MFunc &func) {
  CFG cfg{func};
  LiveVariables live{cfg, func};

  for (size_t bb_id = 0; bb_id < func.bbs.size(); bb_id++) {
    auto &bb = func.bbs[bb_id];
    TVec<size_t> dead_instrs;

    for (size_t instr_idp1 = bb.instrs.size(); instr_idp1 > 0; instr_idp1--) {
      const auto instr_id = instr_idp1 - 1;
      const auto w_args = written_args(bb.instrs[instr_id]);

      if (!is_applicable(bb.instrs[instr_id].op)) {
        continue;
      }

      if (w_args.size() != 1 || !w_args[0].isReg()) {
        continue;
      }

      const auto target_uid = reg_to_uid(w_args[0].reg);

      bool is_only_alive_in_this_bb = true;
      for (size_t i = 0; i < func.bbs.size(); i++) {
        if (i != bb_id && live._live[i][target_uid]) {
          is_only_alive_in_this_bb = false;
        }
      }
      if (!is_only_alive_in_this_bb) {
        continue;
      }
      // FIXME: This is a bit too pesimistic
      bool is_read =
          live._liveIn[bb_id][target_uid] || live._liveOut[bb_id][target_uid];

      auto next_use = find_next_use(bb.instrs, target_uid, instr_id + 1);
      is_read |= next_use.is_read;

      if ((next_use.is_write && !next_use.is_read) || !is_read) {
        bb.instrs.erase(bb.instrs.begin() + instr_id);
        continue;
      }
    }
  }
}

void DeadCodeElim::apply(FVec<MFunc> &funcs) {
  ZoneScopedN("DCE");
  for (auto &func : funcs) {
    apply(func);
  }
}

} // namespace foptim::fmir
