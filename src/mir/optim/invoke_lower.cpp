#include "invoke_lower.hpp"
#include "mir/instr.hpp"
#include "utils/bitset.hpp"
#include "utils/todo.hpp"

namespace foptim::fmir {

// same order as defined in VRegTypeEnum
// static constexpr VRegType g_regs[] = {
//     VRegType::A,   VRegType::B,   VRegType::C,   VRegType::D,   VRegType::S,
//     VRegType::SP, VRegType::R8,  VRegType::R9,  VRegType::R10, VRegType::R11,
//     VRegType::R12, VRegType::R13, VRegType::R14, VRegType::R15};

static void transform(IRVec<MInstr> &instrs, size_t start, size_t end,
                      utils::BitSet used_regs) {
  size_t n_args = end - start;

  FVec<MInstr> args;
  MInstr call = instrs[end];
  args.reserve(n_args);

  for (u32 i = 0; i < n_args; i++) {
    args.push_back(instrs.at(start + i));
  }
  instrs.erase(instrs.begin() + (i64)start, instrs.begin() + (i64)end + 1);

  bool return_value_overwrites_eax =
      (call.args[1].isReg() && call.args[1].reg.info.ty == VRegType::A);

  if (used_regs[0] && !return_value_overwrites_eax) {
    auto arg = MArgument{VReg{0, VRegInfo{(VRegType)(1), 8}}, Type::Int64};
    instrs.insert(instrs.begin() + (i64)start, MInstr{Opcode::pop, arg});
  }

  // ret value
  if (call.n_args == 2) {
    // TODO: different sizes
    instrs.insert(
        instrs.begin() + (i64)start,
        MInstr{Opcode::mov, call.args[1], MArgument{VReg::EAX(), Type::Int32}});
  }

  // TODO: calling conv
  // restore locals
  // NOTE: skipping first reg so we can save the result in it
  for (u8 i = 1; i < ((u8)VRegType::R15) - 1; i++) {
    auto reg_ty = (VRegType)(i + 1);
    if (!used_regs[i] || reg_ty == VRegType::SP || reg_ty == VRegType::BP) {
      continue;
    }
    auto arg = MArgument{VReg{0, VRegInfo{reg_ty, 8}}, Type::Int64};
    instrs.insert(instrs.begin() + (i64)start, MInstr{Opcode::pop, arg});
  }

  // cleanup args
  {
    auto sp = MArgument{VReg{0, VRegInfo{VRegType::SP, 8}}, Type::Int64};
    instrs.insert(instrs.begin() + (i64)start,
                  MInstr{Opcode::add, sp, sp, 8 * args.size()});
  }

  // do call
  instrs.insert(instrs.begin() + (i64)start,
                MInstr{Opcode::call, call.args[0]});
  // setup args
  for (auto &arg : args) {
    instrs.insert(instrs.begin() + (i64)start,
                  MInstr{Opcode::push, arg.args[0]});
  }

  // save locals
  for (u8 i = ((u8)VRegType::R15) - 1; i > 0; i--) {
    auto reg_ty = (VRegType)(i - 1 + 1);
    if (reg_ty == VRegType::A && return_value_overwrites_eax) {
      continue;
    }
    if (!used_regs[i - 1] || reg_ty == VRegType::SP || reg_ty == VRegType::BP) {
      continue;
    }
    auto arg = MArgument{VReg{0, VRegInfo{reg_ty, 8}}, Type::Int64};
    instrs.insert(instrs.begin() + (i64)start, MInstr{Opcode::push, arg});
  }
}

static_assert((u8)VRegType::R15 == 15);
static_assert((u8)VRegType::A == 1);

utils::BitSet calculate_used_regs(const MFunc &f) {
  utils::BitSet res{14, false};
  for (const auto &bb : f.bbs) {
    for (const auto &instr : bb.instrs) {
      for (u32 arg_id = 0; arg_id < instr.n_args; arg_id++) {
        const auto &arg = instr.args[arg_id];
        switch (arg.type) {
        case MArgument::ArgumentType::Imm:
        case MArgument::ArgumentType::Label:
        case MArgument::ArgumentType::MemLabel:
        case MArgument::ArgumentType::MemImmLabel:
        case MArgument::ArgumentType::MemImm:
          break;
        case MArgument::ArgumentType::MemVReg:
        case MArgument::ArgumentType::MemImmVReg:
        case MArgument::ArgumentType::VReg: {
          ASSERT(arg.reg.info.ty != VRegType::Virtual);
          res[(u8)arg.reg.info.ty - 1] = true;
          break;
        }
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVRegScale: {
          ASSERT(arg.reg.info.ty != VRegType::Virtual);
          ASSERT(arg.indx.info.ty != VRegType::Virtual);
          res[(u8)arg.reg.info.ty - 1] = true;
          res[(u8)arg.indx.info.ty - 1] = true;
          break;
        }
        case MArgument::ArgumentType::MemImmVRegScale:
        case MArgument::ArgumentType::MemImmVRegVRegScale:
          ASSERT(false);
        }
      }
    }
  }
  return res;
}

void InvokeLower::apply(FVec<MFunc> &funcs) {
  ZoneScopedN("InvokeLower");
  for (auto &func : funcs) {

    // FIXME: needs proper liveness analysis
    auto used_regs = calculate_used_regs(func);
    used_regs[(u8)VRegType::SP - 1] = false;
    used_regs[(u8)VRegType::BP - 1] = false;
    utils::Debug << "used regs: " << used_regs << "\n";

    for (auto &bb : func.bbs) {
      size_t n_instrs = bb.instrs.size();
      for (size_t instr_id = 0; instr_id < n_instrs; instr_id++) {
        if (bb.instrs[instr_id].op != Opcode::arg_setup &&
            bb.instrs[instr_id].op != Opcode::invoke) {
          continue;
        }
        for (size_t instr_end_id = instr_id; instr_end_id < n_instrs;
             instr_end_id++) {
          if (bb.instrs[instr_end_id].op != Opcode::invoke) {
            continue;
          }
          transform(bb.instrs, instr_id, instr_end_id, used_regs);
          // update the n of instrs since the might have changed it
          n_instrs = bb.instrs.size();
          // number of elements
          // instr_id = 0;
          break;
        }
      }
    }
  }
}
} // namespace foptim::fmir
