#include "live_variables.hpp"

#include <deque>

#include "mir/instr.hpp"
#include "utils/bitset.hpp"
#include "utils/set.hpp"

namespace foptim::fmir {

size_t max_vreg_id(const MFunc &func) {
  size_t unique_reg_id = 0;
  for (const auto &bb : func.bbs) {
    for (const auto &instr : bb.instrs) {
      for (u8 i = 0; i < instr.n_args; i++) {
        switch (instr.args[i].type) {
          case MArgument::ArgumentType::VReg:
          case MArgument::ArgumentType::MemVReg:
          case MArgument::ArgumentType::MemImmVReg:
            if (!instr.args[i].reg.is_concrete()) {
              unique_reg_id =
                  std::max(unique_reg_id, instr.args[i].reg.virt_id());
            }
            break;
          case MArgument::ArgumentType::MemVRegVReg:
          case MArgument::ArgumentType::MemImmVRegVReg:
          case MArgument::ArgumentType::MemVRegVRegScale:
          case MArgument::ArgumentType::MemImmVRegScale:
          case MArgument::ArgumentType::MemImmVRegVRegScale:
            if (!instr.args[i].reg.is_concrete()) {
              unique_reg_id =
                  std::max(unique_reg_id, instr.args[i].reg.virt_id());
            }
            if (!instr.args[i].indx.is_concrete()) {
              unique_reg_id =
                  std::max(unique_reg_id, instr.args[i].indx.virt_id());
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
  if (r.is_concrete()) {
    return (size_t)r.c_reg() - 1;
  }
  return (size_t)CReg::N_REGS + r.virt_id();
}

VReg uid_to_reg(size_t id) {
  if (id + 1 < (size_t)CReg::N_REGS) {
    return VReg{(CReg)(id + 1)};
  }
  return VReg{id - (size_t)CReg::N_REGS};
}

void update_def(const MInstr &instr, utils::BitSet<> &def) {
  switch (instr.bop) {
    case GOpcode::GBase:
      switch ((GBaseSubtype)instr.sop) {
        case GBaseSubtype::INVALID:
          return;
        case GBaseSubtype::mov:
        case GBaseSubtype::pop:
          if (instr.args[0].isReg()) {
            def[reg_to_uid(instr.args[0].reg)].set(true);
          }
          return;
        case GBaseSubtype::push:
        case GBaseSubtype::call:
        case GBaseSubtype::arg_setup:
        case GBaseSubtype::ret:
          return;
        case GBaseSubtype::invoke:
          if (instr.n_args > 1 && instr.args[1].isReg()) {
            def[reg_to_uid(instr.args[1].reg)].set(true);
            if (instr.args[1].is_fp()) {
              def[reg_to_uid(VReg::MM0SS())].set(true);
            } else {
              def[reg_to_uid(VReg::EAX())].set(true);
            }
            if (instr.n_args > 2 && instr.args[2].isReg()) {
              def[reg_to_uid(instr.args[2].reg)].set(true);
              if (instr.args[2].is_fp()) {
                def[reg_to_uid(VReg::MM1SS())].set(true);
              } else {
                def[reg_to_uid(VReg::EDX())].set(true);
              }
            }
          }
          return;
      }
    case GOpcode::GJmp:
      switch ((GJumpSubtype)instr.sop) {
        case GJumpSubtype::INVALID:
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
          if (instr.args[0].isReg()) {
            def[reg_to_uid(instr.args[0].reg)].set(true);
          }
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
        case GJumpSubtype::cjmp:
        case GJumpSubtype::jmp:
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
          if (instr.args[0].isReg()) {
            def[reg_to_uid(instr.args[0].reg)].set(true);
          }
          return;
      }
    case GOpcode::GArith:
      switch ((GArithSubtype)instr.sop) {
        case GArithSubtype::INVALID:
          return;
        case GArithSubtype::idiv:
        case GArithSubtype::udiv:
          def[reg_to_uid(instr.args[0].reg)].set(true);
          def[reg_to_uid(instr.args[1].reg)].set(true);
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
          if (instr.args[0].isReg()) {
            def[reg_to_uid(instr.args[0].reg)].set(true);
          }
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
          if (instr.args[0].isReg()) {
            def[reg_to_uid(instr.args[0].reg)].set(true);
          }
          return;
      }
    case GOpcode::GVec:
      switch ((GVecSubtype)instr.sop) {
        case GVecSubtype::INVALID:
          return;
        case GVecSubtype::vadd:
        case GVecSubtype::vsub:
        case GVecSubtype::fmul:
        case GVecSubtype::fdiv:
        case GVecSubtype::ffmadd:
        case GVecSubtype::fxor:
        case GVecSubtype::fAnd:
        case GVecSubtype::fOr:
        case GVecSubtype::fShl:
          if (instr.args[0].isReg()) {
            def[reg_to_uid(instr.args[0].reg)].set(true);
          }
          return;
      }
    case GOpcode::X86:
      switch ((X86Subtype)instr.sop) {
        case X86Subtype::INVALID:
          return;
        case X86Subtype::lea:
        case X86Subtype::vpshuf:
        case X86Subtype::punpckl:
        case X86Subtype::vbroadcast:
        case X86Subtype::lzcnt:
        case X86Subtype::ffmadd132:
        case X86Subtype::ffmadd213:
        case X86Subtype::ffmadd231:
          if (instr.args[0].isReg()) {
            def[reg_to_uid(instr.args[0].reg)].set(true);
          }
          return;
      }
  }
}

namespace {
void update_uses(const MArgument &arg, utils::BitSet<> &uses) {
  switch (arg.type) {
    case MArgument::ArgumentType::VReg:
    case MArgument::ArgumentType::MemVReg:
    case MArgument::ArgumentType::MemImmVReg:
      uses[reg_to_uid(arg.reg)].set(true);
      break;
    case MArgument::ArgumentType::MemImmVRegScale:
      uses[reg_to_uid(arg.indx)].set(true);
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
  switch (instr.bop) {
    case GOpcode::GJmp:
      switch ((GJumpSubtype)instr.sop) {
        case GJumpSubtype::INVALID:
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
          if (!instr.args[0].isReg()) {
            update_uses(instr.args[0], uses);
          }
          update_uses(instr.args[1], uses);
          update_uses(instr.args[2], uses);
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
          update_uses(instr.args[0], uses);
          update_uses(instr.args[1], uses);
          return;
        case GJumpSubtype::cjmp:
          update_uses(instr.args[0], uses);
          return;
        case GJumpSubtype::jmp:
          return;
      }
    case GOpcode::GCMov:
      switch ((GCMovSubtype)instr.sop) {
        case GCMovSubtype::INVALID:
          return;
        case GCMovSubtype::cmov:
          if (!instr.args[0].isReg()) {
            update_uses(instr.args[0], uses);
          }
          update_uses(instr.args[1], uses);
          update_uses(instr.args[2], uses);
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
          if (!instr.args[0].isReg()) {
            update_uses(instr.args[0], uses);
          }
          update_uses(instr.args[1], uses);
          update_uses(instr.args[2], uses);
          update_uses(instr.args[3], uses);
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
        case GConvSubtype::itrunc:
        case GConvSubtype::F64_ext:
        case GConvSubtype::F32_trunc:
        case GConvSubtype::mov_zx:
        case GConvSubtype::mov_sx:
          if (!instr.args[0].isReg()) {
            update_uses(instr.args[0], uses);
          }
          update_uses(instr.args[1], uses);
          return;
      }
    case GOpcode::GArith:
      switch ((GArithSubtype)instr.sop) {
        case GArithSubtype::INVALID:
          return;
        case GArithSubtype::abs:
          if (!instr.args[0].isReg()) {
            update_uses(instr.args[0], uses);
          }
          update_uses(instr.args[1], uses);
          return;
        case GArithSubtype::shl2:
        case GArithSubtype::shr2:
        case GArithSubtype::sar2:
        case GArithSubtype::land2:
        case GArithSubtype::lor2:
        case GArithSubtype::lxor2:
        case GArithSubtype::add2:
        case GArithSubtype::sub2:
        case GArithSubtype::mul2:
          update_uses(instr.args[0], uses);
          update_uses(instr.args[1], uses);
          return;
        case GArithSubtype::not1:
        case GArithSubtype::neg1:
          update_uses(instr.args[0], uses);
          return;
        case GArithSubtype::idiv:
        case GArithSubtype::udiv:
          // 0 and 1 are fixed regs
          update_uses(instr.args[2], uses);
          update_uses(instr.args[3], uses);
          return;
        case GArithSubtype::smul3:
          if (!instr.args[0].isReg()) {
            update_uses(instr.args[0], uses);
          }
          update_uses(instr.args[1], uses);
          update_uses(instr.args[2], uses);
          return;
      }
    case GOpcode::GVec:
      switch ((GVecSubtype)instr.sop) {
        case GVecSubtype::INVALID:
          return;
        case GVecSubtype::ffmadd:
          if (!instr.args[0].isReg()) {
            update_uses(instr.args[0], uses);
          }
          update_uses(instr.args[1], uses);
          update_uses(instr.args[2], uses);
          update_uses(instr.args[3], uses);
          return;
        case GVecSubtype::vadd:
        case GVecSubtype::vsub:
        case GVecSubtype::fmul:
        case GVecSubtype::fdiv:
        case GVecSubtype::fxor:
        case GVecSubtype::fAnd:
        case GVecSubtype::fOr:
        case GVecSubtype::fShl:
          if (!instr.args[0].isReg()) {
            update_uses(instr.args[0], uses);
          }
          update_uses(instr.args[1], uses);
          update_uses(instr.args[2], uses);
          return;
      }
    case GOpcode::GBase:
      switch ((GBaseSubtype)instr.sop) {
        case GBaseSubtype::INVALID:
          return;
        case GBaseSubtype::mov:
          if (!instr.args[0].isReg()) {
            update_uses(instr.args[0], uses);
          }
          update_uses(instr.args[1], uses);
          return;
        case GBaseSubtype::call:
        case GBaseSubtype::invoke:
          update_uses(instr.args[0], uses);
          if (instr.n_args > 1 && !instr.args[1].isReg()) {
            update_uses(instr.args[1], uses);
            if (instr.n_args > 2 && !instr.args[2].isReg()) {
              update_uses(instr.args[2], uses);
            }
          }
          return;
        case GBaseSubtype::pop:
          if (!instr.args[0].isReg()) {
            update_uses(instr.args[0], uses);
          }
          return;
        case GBaseSubtype::push:
          update_uses(instr.args[0], uses);
          return;
        case GBaseSubtype::ret:
          if (instr.n_args > 0) {
            update_uses(instr.args[0], uses);
            if (instr.n_args > 1) {
              update_uses(instr.args[1], uses);
            }
          }
          return;
        case GBaseSubtype::arg_setup:
          if (!instr.args[1].isReg()) {
            update_uses(instr.args[1], uses);
          }
          update_uses(instr.args[0], uses);
          return;
      }
    case GOpcode::X86:
      switch ((X86Subtype)instr.sop) {
        case X86Subtype::INVALID:
          return;
        case X86Subtype::lea:
        case X86Subtype::lzcnt:
          if (!instr.args[0].isReg()) {
            update_uses(instr.args[0], uses);
          }
          update_uses(instr.args[1], uses);
          return;
        case X86Subtype::punpckl:
        case X86Subtype::vpshuf:
        case X86Subtype::vbroadcast:
          if (!instr.args[0].isReg()) {
            update_uses(instr.args[0], uses);
          }
          update_uses(instr.args[1], uses);
          update_uses(instr.args[2], uses);
          return;

        case X86Subtype::ffmadd132:
        case X86Subtype::ffmadd213:
        case X86Subtype::ffmadd231:
          if (!instr.args[0].isReg()) {
            update_uses(instr.args[0], uses);
          }
          update_uses(instr.args[1], uses);
          update_uses(instr.args[2], uses);
          update_uses(instr.args[3], uses);
          return;
      }
  }
}
}  // namespace

void LiveVariables::update(const fmir::MFunc &func) {
  ZoneScopedN("LiveVariables::update");

  TVec<utils::BitSet<>> upwExp;
  TVec<utils::BitSet<>> defs;

  const auto max_id = max_vreg_id(func);
  const auto n_unique_regs = (u8)CReg::N_REGS + max_id + 1;

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

  _live.resize(func.bbs.size(), utils::BitSet{n_unique_regs, false});
  for (size_t i = 0; i < cfg.bbrs.size(); i++) {
    // we also add defs to make sure we alos get variables taht live shorter
    // then 1 block
    _live[i].assign(_liveIn[i]).add(_liveOut[i]).add(defs[i]);
  }
}

bool LiveVariables::isAlive(const VReg &reg, size_t bb_id) {
  auto id = reg_to_uid(reg);
  if (id >= _live.at(bb_id).bit_size()) {
    TODO("REIMPL");
    // fmt::println(
    //     "Faled to index into live variables with reg {} which gets id {}",
    //     req, id);
    ASSERT(false);
  }
  return _live.at(bb_id)[id];
}

NextUseResult find_next_use(const IRVec<MInstr> &instrs, size_t search_reg_id,
                            size_t start_instr, TVec<ArgData> &args_temp) {
  NextUseResult res{.is_write = false, .is_read = false, .index = 0};
  args_temp.clear();

  for (auto i = start_instr; i < instrs.size(); i++) {
    if (instrs[i].is(GBaseSubtype::call) ||
        instrs[i].is(GBaseSubtype::invoke)) {
      // TODO: this could be more specific since certain CCs can only read/write
      // certain args legaly
      if (instrs[i].n_args > 1) {
        res.is_write = search_reg_id == reg_to_uid(instrs[i].args[1].reg);
        res.is_write |= instrs[i].args[1].is_fp()
                            ? search_reg_id == reg_to_uid(VReg::MM0SS())
                            : search_reg_id == reg_to_uid(VReg::EAX());
      }
      res.index = i;
    }
    if (!res.is_write) {
      args_temp.clear();
      written_args(instrs[i], args_temp);
      for (auto arg : args_temp) {
        if (arg.arg.isReg() && reg_to_uid(arg.arg.reg) == search_reg_id) {
          res.is_write = true;
          res.index = i;
        }
      }
    }
    if (!res.is_read) {
      for (size_t arg_id = 0; arg_id < instrs[i].n_args; arg_id++) {
        const auto &argy = instrs[i].args[arg_id];
        switch (argy.type) {
          case MArgument::ArgumentType::Imm:
          case MArgument::ArgumentType::MemImm:
          case MArgument::ArgumentType::Label:
          case MArgument::ArgumentType::MemLabel:
          case MArgument::ArgumentType::MemImmLabel:
            // here we need to skip vreg
          case MArgument::ArgumentType::VReg:
            break;
          case MArgument::ArgumentType::MemVReg:
          case MArgument::ArgumentType::MemImmVReg:
            if (reg_to_uid(argy.reg) == search_reg_id) {
              res.is_read = true;
              res.index = i;
            }
            break;
          case MArgument::ArgumentType::MemVRegVReg:
          case MArgument::ArgumentType::MemImmVRegVReg:
          case MArgument::ArgumentType::MemVRegVRegScale:
          case MArgument::ArgumentType::MemImmVRegVRegScale:
            if (reg_to_uid(argy.reg) == search_reg_id ||
                reg_to_uid(argy.indx) == search_reg_id) {
              res.is_read = true;
              res.index = i;
            }
            break;
          case MArgument::ArgumentType::MemImmVRegScale:
            if (reg_to_uid(argy.indx) == search_reg_id) {
              res.is_read = true;
              res.index = i;
            }
            break;
        }
      }
    }
    if (!res.is_read) {
      args_temp.clear();
      read_args(instrs[i], args_temp);
      for (auto arg : args_temp) {
        if (arg.arg.isReg() && reg_to_uid(arg.arg.reg) == search_reg_id) {
          res.is_read = true;
          res.index = i;
        }
      }
    }
    if (res.is_read) {
      auto instr = instrs[i];
      if (instrs[i].is(GVecSubtype::fxor) && instr.args[1].isReg() &&
          reg_to_uid(instr.args[1].reg) == search_reg_id &&
          instr.args[1] == instr.args[2]) {
        res.is_read = false;
      }
      if (instrs[i].is(GArithSubtype::lxor2) && instr.args[0].isReg() &&
          reg_to_uid(instr.args[0].reg) == search_reg_id &&
          instr.args[1] == instr.args[2]) {
        res.is_read = false;
      }
    }
    if (res.is_read || res.is_write) {
      break;
    }
  }
  return res;
}

// LINEAR LIFETIMES AFTER
// void close_ranges(VReg reg, size_t instr_id, u32 bb_id,
//                   TVec<std::pair<size_t, size_t>> &started_ranges,
//                   TMap<VReg, LinearRangeSet> &ranges) {
//   auto reg_id = reg_to_uid(reg);
//   for (size_t start_idp1 = started_ranges.size(); start_idp1 > 0;
//        start_idp1--) {
//     const auto &[start_reg_id, start_pos] = started_ranges[start_idp1 - 1];
//     if (start_reg_id == reg_id) {
//       ranges[reg].update(LinearRange::inBB(bb_id, instr_id, start_pos));
//       started_ranges.erase(started_ranges.begin() + start_idp1 - 1);
//     }
//   }
// }
// void open_ranges(MArgument &arg, size_t instr_id,
//                  TVec<std::pair<size_t, size_t>> &started_ranges) {
//   switch (arg.type) {
//   case MArgument::ArgumentType::Imm:
//   case MArgument::ArgumentType::Label:
//   case MArgument::ArgumentType::MemLabel:
//   case MArgument::ArgumentType::MemImmLabel:
//   case MArgument::ArgumentType::MemImm:
//     break;
//   case MArgument::ArgumentType::VReg:
//   case MArgument::ArgumentType::MemImmVReg:
//   case MArgument::ArgumentType::MemVReg:
//     started_ranges.emplace_back(reg_to_uid(arg.reg), instr_id);
//     break;
//   case MArgument::ArgumentType::MemImmVRegScale:
//     started_ranges.emplace_back(reg_to_uid(arg.indx), instr_id);
//     break;
//   case MArgument::ArgumentType::MemVRegVReg:
//   case MArgument::ArgumentType::MemImmVRegVReg:
//   case MArgument::ArgumentType::MemVRegVRegScale:
//   case MArgument::ArgumentType::MemImmVRegVRegScale:
//     started_ranges.emplace_back(reg_to_uid(arg.reg), instr_id);
//     started_ranges.emplace_back(reg_to_uid(arg.indx), instr_id);
//     break;
//   }
// }

TMap<VReg, LinearRangeSet> linear_lifetime(const MFunc &func) {
  ZoneScopedN("LinearLifetimes");
  TSet<VReg> all_used_regs;

  for (const auto &bb : func.bbs) {
    for (const auto &instr : bb.instrs) {
      for (u32 arg_i = 0; arg_i < instr.n_args; arg_i++) {
        const auto &arg = instr.args[arg_i];
        switch (arg.type) {
          case MArgument::ArgumentType::Imm:
          case MArgument::ArgumentType::Label:
          case MArgument::ArgumentType::MemLabel:
          case MArgument::ArgumentType::MemImmLabel:
          case MArgument::ArgumentType::MemImm:
            break;
          case MArgument::ArgumentType::VReg:
          case MArgument::ArgumentType::MemVReg:
          case MArgument::ArgumentType::MemImmVReg:
            all_used_regs.insert(arg.reg);
            break;
          case MArgument::ArgumentType::MemVRegVReg:
          case MArgument::ArgumentType::MemImmVRegVReg:
          case MArgument::ArgumentType::MemVRegVRegScale:
          case MArgument::ArgumentType::MemImmVRegVRegScale:
            all_used_regs.insert(arg.reg);
            all_used_regs.insert(arg.indx);
            break;
          case MArgument::ArgumentType::MemImmVRegScale:
            all_used_regs.insert(arg.indx);
            break;
        }
      }
    }
  }
  CFG cfg{func};
  LiveVariables live{cfg, func};
  TMap<VReg, LinearRangeSet> ranges;
  TVec<ArgData> helper;
  helper.reserve(4);

  // this is used later one to find where the first def is
  for (size_t bb_id = 0; bb_id < func.bbs.size(); bb_id++) {
    const auto &alive = live._live[bb_id];
    const auto &aliveIn = live._liveIn[bb_id];
    const auto &aliveOut = live._liveOut[bb_id];
    // for (auto reg_id : alive) {
    // auto reg = uid_to_reg(reg_id);
    for (const auto &reg : all_used_regs) {
      auto reg_id = reg_to_uid(reg);
      if (alive[reg_id]) {
        size_t start_instr = 0;
        size_t search_instr = 0;
        ranges[reg];

        if (!aliveIn[reg_id]) {
          auto res = find_next_use(func.bbs[bb_id].instrs, reg_id, 0, helper);
          ASSERT(res.is_write);
          ASSERT(!res.is_read);
          start_instr = res.index;
          search_instr = res.index;
        } else {
          auto res = find_next_use(func.bbs[bb_id].instrs, reg_id, 0, helper);
          if (res.is_read) {
            ranges[reg].update(
                LinearRange::inBB(bb_id, start_instr, res.index + 1));
            search_instr = res.index;
          }
          if (res.is_write) {
            start_instr = res.index;
            search_instr = res.index;
          }
        }

        while (true) {
          auto res = find_next_use(func.bbs[bb_id].instrs, reg_id,
                                   search_instr + 1, helper);
          if (!res.is_read && !res.is_write) {
            if (aliveOut[reg_id]) {
              ranges[reg].update(LinearRange::inBB(
                  bb_id, start_instr + 1, func.bbs[bb_id].instrs.size() + 2));
            } else {
              ranges[reg].update(
                  LinearRange::inBB(bb_id, start_instr + 1, start_instr + 1));
            }
            break;
          }
          if (res.is_read) {
            ranges[reg].update(
                LinearRange::inBB(bb_id, start_instr + 1, res.index + 1));
            search_instr = res.index;
          }
          if (res.is_write) {
            // TODO: fix if we got 2 writes following each other otherwise it
            // would be discarded teh first one
            ranges[reg].update(
                LinearRange::inBB(bb_id, start_instr + 1, start_instr + 1));
            start_instr = res.index;
            search_instr = res.index;
          }
        }

        // if its alive out and we have multiple terminators we need to makr
        // it alive at each
        if (aliveOut[reg_id]) {
          for (size_t instr_indx = 0;
               instr_indx < func.bbs[bb_id].instrs.size(); instr_indx++) {
            auto instr = func.bbs[bb_id].instrs[instr_indx];
            if (instr.is(GJumpSubtype::cjmp_int_slt) ||
                instr.is(GJumpSubtype::cjmp_int_sge) ||
                instr.is(GJumpSubtype::cjmp_int_sle) ||
                instr.is(GJumpSubtype::cjmp_int_sgt) ||
                instr.is(GJumpSubtype::cjmp_int_ult) ||
                instr.is(GJumpSubtype::cjmp_int_ule) ||
                instr.is(GJumpSubtype::cjmp_int_ugt) ||
                instr.is(GJumpSubtype::cjmp_int_uge) ||
                instr.is(GJumpSubtype::cjmp_int_ne) ||
                instr.is(GJumpSubtype::cjmp_int_eq) ||
                instr.is(GJumpSubtype::cjmp_flt_oeq) ||
                instr.is(GJumpSubtype::cjmp_flt_ogt) ||
                instr.is(GJumpSubtype::cjmp_flt_oge) ||
                instr.is(GJumpSubtype::cjmp_flt_olt) ||
                instr.is(GJumpSubtype::cjmp_flt_ole) ||
                instr.is(GJumpSubtype::cjmp_flt_one) ||
                instr.is(GJumpSubtype::cjmp_flt_ord) ||
                instr.is(GJumpSubtype::cjmp_flt_uno) ||
                instr.is(GJumpSubtype::cjmp_flt_ueq) ||
                instr.is(GJumpSubtype::cjmp_flt_ugt) ||
                instr.is(GJumpSubtype::cjmp_flt_uge) ||
                instr.is(GJumpSubtype::cjmp_flt_ult) ||
                instr.is(GJumpSubtype::cjmp_flt_ule) ||
                instr.is(GJumpSubtype::cjmp_flt_une) ||
                instr.is(GJumpSubtype::cjmp)) {
              size_t closest_index = 0;
              for (auto range : ranges[reg].ranges) {
                if (range.end.bb_indx == bb_id &&
                    range.end.instr_indx <= instr_indx &&
                    range.end.instr_indx > closest_index) {
                  closest_index = range.end.instr_indx;
                }
              }
              ranges[reg].update(
                  LinearRange::inBB(bb_id, closest_index, instr_indx));
            }
          }
        }

        if (alive[reg_id] && ranges[reg].ranges.empty()) {
          ranges[reg].update(
              LinearRange::inBB(bb_id, start_instr + 1, start_instr + 2));
        }
      }
    }
  }
  return ranges;
}

TMap<VReg, TSet<size_t>> reg_coll(const MFunc &func) {
  ZoneScopedN("regColl");
  TSet<VReg> all_used_regs;
  CFG cfg{func};
  LiveVariables live{cfg, func};
  TMap<VReg, TSet<size_t>> ranges;
  TVec<ArgData> helper;
  helper.reserve(8);

  for (const auto &bb : func.bbs) {
    for (const auto &instr : bb.instrs) {
      for (u32 arg_i = 0; arg_i < instr.n_args; arg_i++) {
        const auto &arg = instr.args[arg_i];
        switch (arg.type) {
          case MArgument::ArgumentType::Imm:
          case MArgument::ArgumentType::Label:
          case MArgument::ArgumentType::MemLabel:
          case MArgument::ArgumentType::MemImmLabel:
          case MArgument::ArgumentType::MemImm:
            break;
          case MArgument::ArgumentType::VReg:
          case MArgument::ArgumentType::MemVReg:
          case MArgument::ArgumentType::MemImmVReg:
            ranges[arg.reg];
            all_used_regs.insert(arg.reg);
            break;
          case MArgument::ArgumentType::MemVRegVReg:
          case MArgument::ArgumentType::MemImmVRegVReg:
          case MArgument::ArgumentType::MemVRegVRegScale:
          case MArgument::ArgumentType::MemImmVRegVRegScale:
            ranges[arg.reg];
            ranges[arg.indx];
            all_used_regs.insert(arg.reg);
            all_used_regs.insert(arg.indx);
            break;
          case MArgument::ArgumentType::MemImmVRegScale:
            ranges[arg.indx];
            all_used_regs.insert(arg.indx);
            break;
        }
      }
    }
  }

  for (size_t bb_id = 0; bb_id < func.bbs.size(); bb_id++) {
    // const auto &alive = live._live[bb_id];
    // const auto &aliveIn = live._liveIn[bb_id];
    const auto &aliveOut = live._liveOut[bb_id];
    const auto &bb = func.bbs[bb_id];

    auto curr_live = aliveOut;

    for (size_t instridp1 = bb.instrs.size(); instridp1 > 0; instridp1--) {
      // fmt::println("{}", curr_live);
      // fmt::println("{}", bb.instrs[instridp1 - 1]);
      const auto &instr = bb.instrs[instridp1 - 1];
      if (instr.is(GJumpSubtype::cjmp_int_slt) ||
          instr.is(GJumpSubtype::cjmp_int_sge) ||
          instr.is(GJumpSubtype::cjmp_int_sle) ||
          instr.is(GJumpSubtype::cjmp_int_sgt) ||
          instr.is(GJumpSubtype::cjmp_int_ult) ||
          instr.is(GJumpSubtype::cjmp_int_ule) ||
          instr.is(GJumpSubtype::cjmp_int_ugt) ||
          instr.is(GJumpSubtype::cjmp_int_uge) ||
          instr.is(GJumpSubtype::cjmp_int_ne) ||
          instr.is(GJumpSubtype::cjmp_int_eq) ||
          instr.is(GJumpSubtype::cjmp_flt_oeq) ||
          instr.is(GJumpSubtype::cjmp_flt_ogt) ||
          instr.is(GJumpSubtype::cjmp_flt_oge) ||
          instr.is(GJumpSubtype::cjmp_flt_olt) ||
          instr.is(GJumpSubtype::cjmp_flt_ole) ||
          instr.is(GJumpSubtype::cjmp_flt_one) ||
          instr.is(GJumpSubtype::cjmp_flt_ord) ||
          instr.is(GJumpSubtype::cjmp_flt_uno) ||
          instr.is(GJumpSubtype::cjmp_flt_ueq) ||
          instr.is(GJumpSubtype::cjmp_flt_ugt) ||
          instr.is(GJumpSubtype::cjmp_flt_uge) ||
          instr.is(GJumpSubtype::cjmp_flt_ult) ||
          instr.is(GJumpSubtype::cjmp_flt_ule) ||
          instr.is(GJumpSubtype::cjmp_flt_une) ||
          instr.is(GJumpSubtype::cjmp) || instr.is(GJumpSubtype::jmp)) {
        curr_live.add(aliveOut);
      }
      helper.clear();
      written_args(instr, helper);
      // auto all defs
      for (auto written : helper) {
        if (written.arg.isReg()) {
          for (auto r : curr_live) {
            ranges[uid_to_reg(r)].insert(reg_to_uid(written.arg.reg));
            ranges[written.arg.reg].insert(r);
          }
          curr_live[reg_to_uid(written.arg.reg)].set(false);
        }
      }
      // special case is call or invoke in which case also all args are read
      //   iff the call target is a register and not a constant
      if ((instr.is(GBaseSubtype::call) || instr.is(GBaseSubtype::invoke)) &&
          instr.args[0].isReg()) {
        for (auto arg_ip1 = instridp1 - 1; arg_ip1 > 0; arg_ip1--) {
          const auto &arg_instr = bb.instrs[arg_ip1 - 1];
          if (!arg_instr.is(GBaseSubtype::arg_setup)) {
            break;
          }
          if (arg_instr.n_args > 1) {
            ASSERT(arg_instr.args[1].isReg());
            curr_live[reg_to_uid(arg_instr.args[1].reg)].set(true);
          }
        }
      }

      // find all uses
      for (auto written : helper) {
        switch (written.arg.type) {
          case MArgument::ArgumentType::Imm:
          case MArgument::ArgumentType::Label:
          case MArgument::ArgumentType::MemLabel:
          case MArgument::ArgumentType::MemImmLabel:
          case MArgument::ArgumentType::MemImm:
          case MArgument::ArgumentType::VReg:
            break;
          case MArgument::ArgumentType::MemVReg:
          case MArgument::ArgumentType::MemImmVReg:
            // fmt::println("{} USE {} ", curr_live, written.arg.reg);
            for (auto r : curr_live) {
              ranges[uid_to_reg(r)].insert(reg_to_uid(written.arg.reg));
              ranges[written.arg.reg].insert(r);
            }
            curr_live[reg_to_uid(written.arg.reg)].set(true);
            break;
          case MArgument::ArgumentType::MemImmVRegScale:
            // fmt::println("{} USE {} ", curr_live, written.arg.indx);
            for (auto r : curr_live) {
              ranges[uid_to_reg(r)].insert(reg_to_uid(written.arg.indx));
              ranges[written.arg.indx].insert(r);
            }
            curr_live[reg_to_uid(written.arg.indx)].set(true);
            break;
          case MArgument::ArgumentType::MemVRegVReg:
          case MArgument::ArgumentType::MemImmVRegVReg:
          case MArgument::ArgumentType::MemVRegVRegScale:
          case MArgument::ArgumentType::MemImmVRegVRegScale:
            // fmt::println("{} USE {} ", curr_live, written.arg.reg);
            // fmt::println("{} USE {} ", curr_live, written.arg.indx);
            for (auto r : curr_live) {
              ranges[uid_to_reg(r)].insert(reg_to_uid(written.arg.reg));
              ranges[uid_to_reg(r)].insert(reg_to_uid(written.arg.indx));
              ranges[written.arg.reg].insert(r);
              ranges[written.arg.indx].insert(r);
            }
            ranges[written.arg.reg].insert(reg_to_uid(written.arg.indx));
            ranges[written.arg.indx].insert(reg_to_uid(written.arg.reg));
            curr_live[reg_to_uid(written.arg.reg)].set(true);
            curr_live[reg_to_uid(written.arg.indx)].set(true);
            break;
        }
      }
      helper.clear();
      read_args(instr, helper);
      for (auto read : helper) {
        switch (read.arg.type) {
          case MArgument::ArgumentType::Imm:
          case MArgument::ArgumentType::Label:
          case MArgument::ArgumentType::MemLabel:
          case MArgument::ArgumentType::MemImmLabel:
          case MArgument::ArgumentType::MemImm:
            break;
          case MArgument::ArgumentType::VReg:
          case MArgument::ArgumentType::MemVReg:
          case MArgument::ArgumentType::MemImmVReg:
            // fmt::println("{} USE {} ", curr_live, read.arg.reg);
            for (auto r : curr_live) {
              ranges[uid_to_reg(r)].insert(reg_to_uid(read.arg.reg));
              ranges[read.arg.reg].insert(r);
            }
            curr_live[reg_to_uid(read.arg.reg)].set(true);
            break;
          case MArgument::ArgumentType::MemImmVRegScale:
            // fmt::println("{} USE {} ", curr_live, read.arg.indx);
            for (auto r : curr_live) {
              ranges[uid_to_reg(r)].insert(reg_to_uid(read.arg.indx));
              ranges[read.arg.indx].insert(r);
            }
            curr_live[reg_to_uid(read.arg.indx)].set(true);
            break;
          case MArgument::ArgumentType::MemVRegVReg:
          case MArgument::ArgumentType::MemImmVRegVReg:
          case MArgument::ArgumentType::MemVRegVRegScale:
          case MArgument::ArgumentType::MemImmVRegVRegScale:
            // fmt::println("{} USE {} ", curr_live, read.arg.reg);
            // fmt::println("{} USE {} ", curr_live, read.arg.indx);
            for (auto r : curr_live) {
              ranges[uid_to_reg(r)].insert(reg_to_uid(read.arg.reg));
              ranges[uid_to_reg(r)].insert(reg_to_uid(read.arg.indx));
              ranges[read.arg.reg].insert(r);
              ranges[read.arg.indx].insert(r);
            }
            ranges[read.arg.reg].insert(reg_to_uid(read.arg.indx));
            ranges[read.arg.indx].insert(reg_to_uid(read.arg.reg));
            curr_live[reg_to_uid(read.arg.reg)].set(true);
            curr_live[reg_to_uid(read.arg.indx)].set(true);
            break;
        }
      }
    }
  }

  return ranges;
}

}  // namespace foptim::fmir
