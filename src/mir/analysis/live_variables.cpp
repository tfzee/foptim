#include "live_variables.hpp"
#include "utils/bitset.hpp"
#include <deque>

namespace foptim::fmir {

static size_t max_vreg_id(const MFunc &func) {
  size_t unique_reg_id = 0;
  for (const auto &bb : func.bbs) {
    for (const auto &instr : bb.instrs) {
      for (u8 i = 0; i < instr.n_args; i++) {
        switch (instr.args[i].type) {
        case MArgument::ArgumentType::VReg:
        case MArgument::ArgumentType::MemVReg:
        case MArgument::ArgumentType::MemImmVReg:
          if (!instr.args[i].reg.info.is_pinned()) {
            unique_reg_id = std::max(unique_reg_id, instr.args[i].reg.id);
          }
          break;
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVRegScale:
        case MArgument::ArgumentType::MemImmVRegScale:
        case MArgument::ArgumentType::MemImmVRegVRegScale:
          if (!instr.args[i].reg.info.is_pinned()) {
            unique_reg_id = std::max(unique_reg_id, instr.args[i].reg.id);
          }
          if (!instr.args[i].indx.info.is_pinned()) {
            unique_reg_id = std::max(unique_reg_id, instr.args[i].indx.id);
          }
          break;
        default:
          break;
        }
      }
    }
  }
  return unique_reg_id;
}

size_t reg_to_uid(VReg r) {
  if (r.info.is_pinned()) {
    return (u8)r.info.ty - 1;
  }
  return (u8)VRegType::N_REGS + r.id;
}
// VReg uid_to_reg(size_t r) {
//   if (r >= (u8)VRegType::N_REGS) {
//     return VReg(r - (u8)VRegType::N_REGS, VRegInfo());
//   }
//   return VReg(0, VRegInfo{(VRegType)(r + 1), Type::Int8});
// }

void update_def(const MInstr &instr, utils::BitSet<> &def) {
  switch (instr.op) {
  // for moves this is correct only if its not pointers
  case Opcode::mov:
  case Opcode::cmov:
  case Opcode::mov_zx:
  case Opcode::mov_sx:
  case Opcode::itrunc:
  case Opcode::lea:
  case Opcode::add:
  case Opcode::shl:
  case Opcode::shr:
  case Opcode::lor:
  case Opcode::land:
  case Opcode::lxor:
  case Opcode::sar:
  case Opcode::sub:
  case Opcode::mul:
  case Opcode::fadd:
  case Opcode::fsub:
  case Opcode::fmul:
  case Opcode::fdiv:
  case Opcode::ffmadd132:
  case Opcode::ffmadd231:
  case Opcode::ffmadd213:
  case Opcode::fxor:
  case Opcode::SI2FL:
  case Opcode::UI2FL:
  case Opcode::FL2SI:
  case Opcode::FL2UI:
  case Opcode::pop:
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
    if (instr.args[0].isReg()) {
      def[reg_to_uid(instr.args[0].reg)].set(true);
    }
    break;
  case Opcode::invoke:
    if (instr.args[1].isReg()) {
      def[reg_to_uid(instr.args[1].reg)].set(true);
    }
    break;
  case Opcode::idiv:
    def[reg_to_uid(instr.args[0].reg)].set(true);
    def[reg_to_uid(instr.args[1].reg)].set(true);
    break;
  case Opcode::arg_setup:
    if (instr.args[1].isReg()) {
      def[reg_to_uid(instr.args[1].reg)].set(true);
    }
  case Opcode::cjmp_int_slt:
  case Opcode::cjmp_int_sge:
  case Opcode::cjmp_int_sle:
  case Opcode::cjmp_int_sgt:
  case Opcode::cjmp_int_ult:
  case Opcode::cjmp_int_ule:
  case Opcode::cjmp_int_ugt:
  case Opcode::cjmp_int_uge:
  case Opcode::cjmp_int_ne:
  case Opcode::cjmp_int_eq:
  case Opcode::cjmp_flt_oeq:
  case Opcode::cjmp_flt_ogt:
  case Opcode::cjmp_flt_oge:
  case Opcode::cjmp_flt_olt:
  case Opcode::cjmp_flt_ole:
  case Opcode::cjmp_flt_one:
  case Opcode::cjmp_flt_ord:
  case Opcode::cjmp_flt_uno:
  case Opcode::cjmp_flt_ueq:
  case Opcode::cjmp_flt_ugt:
  case Opcode::cjmp_flt_uge:
  case Opcode::cjmp_flt_ult:
  case Opcode::cjmp_flt_ule:
  case Opcode::cjmp_flt_une:
  case Opcode::push:
  case Opcode::cjmp:
  case Opcode::jmp:
  case Opcode::call:
  case Opcode::ret:
    break;
  }
}

void update_uses(const MArgument &arg, utils::BitSet<> &uses) {
  switch (arg.type) {
  case MArgument::ArgumentType::VReg:
  case MArgument::ArgumentType::MemVReg:
  case MArgument::ArgumentType::MemImmVReg:
  case MArgument::ArgumentType::MemImmVRegScale:
    uses[reg_to_uid(arg.reg)].set(true);
    break;
  case MArgument::ArgumentType::MemVRegVReg:
  case MArgument::ArgumentType::MemImmVRegVReg:
  case MArgument::ArgumentType::MemVRegVRegScale:
  case MArgument::ArgumentType::MemImmVRegVRegScale:
    uses[reg_to_uid(arg.reg)].set(true);
    uses[reg_to_uid(arg.indx)].set(true);
    break;
  case MArgument::ArgumentType::MemImm:
  case MArgument::ArgumentType::Label:
  case MArgument::ArgumentType::MemLabel:
  case MArgument::ArgumentType::MemImmLabel:
  case MArgument::ArgumentType::Imm:
    break;
  }
}

void update_uses(const MInstr &instr, utils::BitSet<> &uses) {
  switch (instr.op) {
  // for moves this is only correct if the target is a reg
  case Opcode::mov:
  case Opcode::itrunc:
  case Opcode::mov_zx:
  case Opcode::mov_sx:
  case Opcode::cmov:
  case Opcode::lea:
    if (!instr.args[0].isReg()) {
      update_uses(instr.args[0], uses);
    }
    update_uses(instr.args[1], uses);
    break;
  case Opcode::add:
  case Opcode::shl:
  case Opcode::shr:
  case Opcode::lor:
  case Opcode::land:
  case Opcode::lxor:
  case Opcode::sar:
  case Opcode::sub:
  case Opcode::mul:
  case Opcode::fadd:
  case Opcode::fsub:
  case Opcode::fmul:
  case Opcode::fdiv:
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
    if (!instr.args[0].isReg()) {
      update_uses(instr.args[0], uses);
    }
    update_uses(instr.args[1], uses);
    update_uses(instr.args[2], uses);
    break;
  case Opcode::ffmadd132:
  case Opcode::ffmadd231:
  case Opcode::ffmadd213:
    if (!instr.args[0].isReg()) {
      update_uses(instr.args[0], uses);
    }
    update_uses(instr.args[1], uses);
    update_uses(instr.args[2], uses);
    update_uses(instr.args[3], uses);
    break;
  case Opcode::idiv:
    update_uses(instr.args[2], uses);
    update_uses(instr.args[3], uses);
    break;
  case Opcode::invoke:
  case Opcode::arg_setup:
  case Opcode::ret:
  case Opcode::push:
  case Opcode::cjmp:
  case Opcode::call:
    update_uses(instr.args[0], uses);
    break;
  case Opcode::jmp:
    break;
  case Opcode::pop:
    if (!instr.args[0].isReg()) {
      update_uses(instr.args[0], uses);
    }
    break;
  case Opcode::cjmp_int_slt:
  case Opcode::cjmp_int_sge:
  case Opcode::cjmp_int_sle:
  case Opcode::cjmp_int_sgt:
  case Opcode::cjmp_int_ult:
  case Opcode::cjmp_int_ule:
  case Opcode::cjmp_int_ugt:
  case Opcode::cjmp_int_uge:
  case Opcode::cjmp_int_ne:
  case Opcode::cjmp_int_eq:
  case Opcode::cjmp_flt_oeq:
  case Opcode::cjmp_flt_ogt:
  case Opcode::cjmp_flt_oge:
  case Opcode::cjmp_flt_olt:
  case Opcode::cjmp_flt_ole:
  case Opcode::cjmp_flt_one:
  case Opcode::cjmp_flt_ord:
  case Opcode::cjmp_flt_uno:
  case Opcode::cjmp_flt_ueq:
  case Opcode::cjmp_flt_ugt:
  case Opcode::cjmp_flt_uge:
  case Opcode::cjmp_flt_ult:
  case Opcode::cjmp_flt_ule:
  case Opcode::cjmp_flt_une:
    update_uses(instr.args[0], uses);
    update_uses(instr.args[1], uses);
    break;
  }
}

void LiveVariables::update(const fmir::MFunc &func) {

  TVec<utils::BitSet<>> upwExp;
  TVec<utils::BitSet<>> defs;

  const auto max_id = max_vreg_id(func);
  const auto n_unique_regs = (u8)VRegType::N_REGS + max_id + 1;

  upwExp.resize(func.bbs.size(), utils::BitSet{n_unique_regs, false});
  defs.resize(func.bbs.size(), utils::BitSet{n_unique_regs, false});
  std::deque<u32, utils::TempAlloc<u32>> worklist{};

  utils::BitSet<> helper{n_unique_regs, false};

  for (size_t bb_id = 0; bb_id < func.bbs.size(); bb_id++) {
    worklist.push_front(bb_id);
    const auto &bb = func.bbs[bb_id];
    for (const auto &instr : bb.instrs) {
      // update upwexp
      helper.reset(false);
      update_uses(instr, helper);
      helper.mul_not(defs[bb_id]);
      upwExp[bb_id].add(helper);
      // update defs
      update_def(instr, defs[bb_id]);
    }
  }

  utils::Debug << "FUNC\n" << func << "\n";
  utils::Debug << "\nUPWEXP\n" << upwExp << "\n";
  utils::Debug << "defs\n" << defs << "\n";
  _liveIn.clear();
  _liveOut.clear();
  _liveIn.resize(func.bbs.size(), utils::BitSet{n_unique_regs, false});
  _liveOut.resize(func.bbs.size(), utils::BitSet{n_unique_regs, false});
  utils::BitSet new_liveOut{n_unique_regs, false};
  utils::BitSet new_liveIn{n_unique_regs, false};

  while (!worklist.empty()) {
    u32 curr_id = worklist.front();
    worklist.pop_front();
    new_liveOut.reset(false);

    for (auto succ : cfg.bbrs[curr_id].succ) {
      new_liveOut += _liveIn[succ];
    }
    new_liveIn.assign(new_liveOut).mul_not(defs[curr_id]).add(upwExp[curr_id]);
    // utils::Debug << "Updating " << curr_id << " " << new_liveOut << "  " <<
    // new_liveIn
    //              << "\n";
    // auto test = upwExp[curr_id] + (new_liveOut - defs[curr_id]);
    // assert(test == liveIn[curr_id]);

    if (new_liveOut != _liveOut[curr_id]) {
      _liveOut[curr_id].assign(new_liveOut);
      // worklist.push_back(curr_id);
    }
    if (new_liveIn != _liveIn[curr_id]) {
      _liveIn[curr_id].assign(new_liveIn);
      for (auto pred : cfg.bbrs[curr_id].pred) {
        worklist.push_back(pred);
      }
    }
  }

  utils::Debug << "\nLIVEIN\n" << _liveIn << "\n";
  utils::Debug << "LIVEOUT\n" << _liveOut << "\n";
  _live.resize(func.bbs.size(), utils::BitSet{n_unique_regs, false});
  for (size_t i = 0; i < cfg.bbrs.size(); i++) {
    // we also add defs to make sure we alos get variables taht live shorter
    // then 1 block
    _live[i].assign(_liveIn[i]).add(_liveOut[i]).add(defs[i]);
  }
  utils::Debug << "LIVE\n" << _live << "\n";
}

bool LiveVariables::isAlive(const VReg &reg, size_t bb_id) {
  auto id = reg_to_uid(reg);
  if (id >= _live.at(bb_id).size()) {
    utils::Debug << "Faled to index into live variables with reg " << reg
                 << " which gets id " << id << "\n";
    ASSERT(false);
  }
  return _live.at(bb_id)[id];
}

} // namespace foptim::fmir
