#include "calling_conv.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"
#include "utils/bitset.hpp"
#include "utils/todo.hpp"
#include <ranges>

namespace foptim::fmir {

VRegType caller_saved[] = {
    VRegType::A,    VRegType::C,    VRegType::D,    VRegType::SI,
    VRegType::DI,   VRegType::R8,   VRegType::R9,   VRegType::R10,
    VRegType::R11,  VRegType::mm0,  VRegType::mm1,  VRegType::mm2,
    VRegType::mm3,  VRegType::mm4,  VRegType::mm5,  VRegType::mm6,
    VRegType::mm7,  VRegType::mm8,  VRegType::mm9,  VRegType::mm10,
    VRegType::mm11, VRegType::mm12, VRegType::mm13, VRegType::mm14,
    VRegType::mm15};
// VRegType caller_saved[] = {
//     VRegType::A,    VRegType::B,    VRegType::C,    VRegType::D,
//     VRegType::SI,   VRegType::DI,   VRegType::SP,   VRegType::BP,
//     VRegType::R8,   VRegType::R9,   VRegType::R10,  VRegType::R11,
//     VRegType::R12,  VRegType::R13,  VRegType::R14,  VRegType::R15,
//     VRegType::mm0,  VRegType::mm1,  VRegType::mm2,  VRegType::mm3,
//     VRegType::mm4,  VRegType::mm5,  VRegType::mm6,  VRegType::mm7,
//     VRegType::mm8,  VRegType::mm9,  VRegType::mm10, VRegType::mm11,
//     VRegType::mm12, VRegType::mm13, VRegType::mm14, VRegType::mm15,
// };

VRegType callee_saved[] = {
    VRegType::R12, VRegType::R13, VRegType::R14, VRegType::R15,
    VRegType::B,   VRegType::SP,  VRegType::BP,
};

static void save_regs_callee(MFunc &func, LiveVariables &lives, CFG &cfg) {
  auto first_bb = func.bbs[0];

  // store all the regs in the initial bb
  for (auto reg_ty : callee_saved | std::views::reverse) {
    if (!lives.isAlive(VReg{reg_ty}, 0) || reg_ty == VRegType::SP ||
        reg_ty == VRegType::BP) {
      continue;
    }
    auto arg = MArgument{VReg{0, VRegInfo{reg_ty, Type::Int64}}, Type::Int64};
    first_bb.instrs.insert(first_bb.instrs.begin() + 0,
                           MInstr{Opcode::push, arg});
  }

  // restore in *every* exiting basic block we first need to find these
  // should be all cfg blocks without any successors??

  for (size_t bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
    if (cfg.bbrs[bb_id].succ.size() != 0) {
      continue;
    }
    for (auto reg_ty : callee_saved) {
      if (!lives.isAlive(VReg{reg_ty}, bb_id) || reg_ty == VRegType::SP ||
          reg_ty == VRegType::BP) {
        continue;
      }
      auto arg = MArgument{VReg{0, VRegInfo{reg_ty, Type::Int64}}, Type::Int64};
      first_bb.instrs.insert(first_bb.instrs.begin() + 0,
                             MInstr{Opcode::pop, arg});
    }
  }
}

static void save_args(IRVec<MInstr> &instrs, LiveVariables &lives, size_t start,
                      size_t bb_id, bool return_value_overwrites_eax) {
  for (auto reg_ty : caller_saved | std::views::reverse) {
    if (reg_ty == VRegType::A && return_value_overwrites_eax) {
      continue;
    }
    if (!lives.isAlive(VReg{reg_ty}, bb_id) || reg_ty == VRegType::SP ||
        reg_ty == VRegType::BP) {
      continue;
    }
    auto arg = MArgument{VReg{0, VRegInfo{reg_ty, Type::Int64}}, Type::Int64};
    instrs.insert(instrs.begin() + (i64)start, MInstr{Opcode::push, arg});
  }
}

static void restore_args(IRVec<MInstr> &instrs, LiveVariables &lives,
                         size_t start, size_t bb_id,
                         bool return_value_overwrites_eax, MInstr &call) {
  if (lives.isAlive(VReg{VRegType::A}, bb_id) && !return_value_overwrites_eax) {
    auto arg =
        MArgument{VReg{0, VRegInfo{(VRegType)(1), Type::Int64}}, Type::Int64};
    instrs.insert(instrs.begin() + (i64)start, MInstr{Opcode::pop, arg});
  }

  // ret value
  if (call.n_args == 2) {
    // TODO: different sizes
    auto ret_type = call.args[1].ty;

    bool is_float = ret_type == Type::Float32 || ret_type == Type::Float64;
    auto reg_type = is_float ? VRegType::mm0 : VRegType::A;
    instrs.insert(
        instrs.begin() + (i64)start,
        MInstr{Opcode::mov, call.args[1],
               MArgument{VReg{0, VRegInfo{reg_type, ret_type}}, ret_type}});
  }

  for (auto reg_ty : caller_saved) {
    if (!lives.isAlive(VReg{reg_ty}, bb_id) || reg_ty == VRegType::A ||
        reg_ty == VRegType::SP || reg_ty == VRegType::BP) {
      continue;
    }
    auto arg = MArgument{VReg{0, VRegInfo{reg_ty, Type::Int64}}, Type::Int64};
    instrs.insert(instrs.begin() + (i64)start, MInstr{Opcode::pop, arg});
  }
}

static void transform_call(IRVec<MInstr> &instrs, size_t start, size_t end,
                           size_t bb_id, LiveVariables &lives) {

  size_t n_args = end - start;

  TVec<MInstr> args;
  MInstr call = instrs[end];
  args.reserve(n_args);

  for (u32 i = 0; i < n_args; i++) {
    args.push_back(instrs.at(start + i));
  }
  instrs.erase(instrs.begin() + (i64)start, instrs.begin() + (i64)end + 1);

  bool return_value_overwrites_eax =
      (call.args[1].isReg() && call.args[1].reg.info.ty == VRegType::A);

  restore_args(instrs, lives, start, bb_id, return_value_overwrites_eax, call);

  // cleanup args
  {
    auto sp =
        MArgument{VReg{0, VRegInfo{VRegType::SP, Type::Int64}}, Type::Int64};
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
  save_args(instrs, lives, start, bb_id, return_value_overwrites_eax);
}

static_assert((u8)VRegType::R15 == 16);
static_assert((u8)VRegType::A == 1);

utils::BitSet<> calculate_used_regs(const MFunc &f) {
  utils::BitSet<> res{29, false};
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
          res[(u8)arg.reg.info.ty - 1].set(true);
          break;
        }
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVRegScale: {
          ASSERT(arg.reg.info.ty != VRegType::Virtual);
          ASSERT(arg.indx.info.ty != VRegType::Virtual);
          res[(u8)arg.reg.info.ty - 1].set(true);
          res[(u8)arg.indx.info.ty - 1].set(true);
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

void CallingConv::second_stage(FVec<MFunc> &funcs) {
  ZoneScopedN("InvokeLower");
  for (auto &func : funcs) {
    CFG cfg(func);
    LiveVariables lives(cfg, func);

    save_regs_callee(func, lives, cfg);

    size_t bb_id = 0;
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
          // FIXME: needs proper liveness analysis
          transform_call(bb.instrs, instr_id, instr_end_id, bb_id, lives);
          // update the n of instrs since the might have changed it
          n_instrs = bb.instrs.size();
          // number of elements
          // instr_id = 0;
          break;
        }
      }
      bb_id++;
    }
  }
}

void gen_arg_mapping(MFunc &func) {
  for (u32 arg_i = 0; arg_i < func.args.size(); arg_i++) {
    ASSERT(!func.args[arg_i].info.is_pinned());
    auto arg_ty = func.arg_tys[arg_i];
    auto instr = MInstr(Opcode::mov, MArgument{func.args[arg_i], arg_ty},
                        MArgument::Mem(VReg::RSP(), 8 * (arg_i + 2), arg_ty));
    func.bbs[0].instrs.insert(func.bbs[0].instrs.begin(), instr);
  }
}

static void function_argument_loading(MFunc &func) { gen_arg_mapping(func); }

void CallingConv::first_stage(FVec<MFunc> &funcs) {
  ZoneScopedN("ArgLower");
  for (auto &func : funcs) {
    function_argument_loading(func);
  }
}

} // namespace foptim::fmir
