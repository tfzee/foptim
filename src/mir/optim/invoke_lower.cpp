#include "invoke_lower.hpp"
#include "mir/instr.hpp"
#include "utils/bitset.hpp"
#include "utils/todo.hpp"

namespace foptim::fmir {

// same order as defined in VRegTypeEnum
static constexpr VRegType g_regs[] = {
    VRegType::A,   VRegType::C,   VRegType::D,   VRegType::S,
    VRegType::R8,  VRegType::R9,  VRegType::R10, VRegType::R11,
    VRegType::R12, VRegType::R13, VRegType::R14, VRegType::R15};

static void transform(FVec<MInstr> &instrs, size_t start, size_t end,
                      utils::BitSet used_regs) {
  size_t n_args = end - start;

  FVec<MInstr> args;
  MInstr call = instrs[end];
  args.reserve(n_args);

  for (u32 i = 0; i < n_args; i++) {
    args.push_back(instrs.at(start + i));
  }
  instrs.erase(instrs.begin() + (i64)start, instrs.begin() + (i64)end + 1);

  if (used_regs[0 + 1]) {
    auto arg = MArgument{VReg{0, VRegInfo{g_regs[0], 8}}, Type::Int64};
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
  for (u8 i = 13; i > 1; i--) {
    if (!used_regs[i]) {
      continue;
    }
    auto arg = MArgument{VReg{0, VRegInfo{g_regs[i - 1], 8}}, Type::Int64};
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
  for (u8 i = 0; i < 13; i++) {
    if (!used_regs[i + 1]) {
      continue;
    }
    auto arg = MArgument{VReg{0, VRegInfo{g_regs[i], 8}}, Type::Int64};
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
          res[(u8)arg.reg.info.ty - 1] = true;
          break;
        }
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVRegScale: {
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

    auto used_regs = calculate_used_regs(func);

    for (auto &bb : func.bbs) {
      const size_t n_instrs = bb.instrs.size();
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
          break;
        }
      }
    }
  }
}
} // namespace foptim::fmir
