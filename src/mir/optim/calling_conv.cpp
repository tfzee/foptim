#include "calling_conv.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"
#include "utils/bitset.hpp"
#include "utils/todo.hpp"
#include <ranges>

namespace foptim::fmir {

utils::BitSet<> calculate_used_regs(const MFunc &f);

constexpr VRegType caller_saved[] = {
    VRegType::A,    VRegType::C,    VRegType::D,    VRegType::SI,
    VRegType::DI,   VRegType::R8,   VRegType::R9,   VRegType::R10,
    VRegType::R11,  VRegType::mm0,  VRegType::mm1,  VRegType::mm2,
    VRegType::mm3,  VRegType::mm4,  VRegType::mm5,  VRegType::mm6,
    VRegType::mm7,  VRegType::mm8,  VRegType::mm9,  VRegType::mm10,
    VRegType::mm11, VRegType::mm12, VRegType::mm13, VRegType::mm14,
    VRegType::mm15};

constexpr VRegType callee_saved[] = {
    VRegType::R12, VRegType::R13, VRegType::R14, VRegType::R15,
    VRegType::B,   VRegType::SP,  VRegType::BP,
};

constexpr VRegType int_arg_reg[] = {
    VRegType::DI, VRegType::SI, VRegType::D,
    VRegType::C,  VRegType::R8, VRegType::R9,
};
constexpr u32 n_int_arg_regs = sizeof(int_arg_reg) / sizeof(int_arg_reg[0]);
constexpr VRegType float_arg_reg[] = {
    VRegType::mm0, VRegType::mm1, VRegType::mm2, VRegType::mm3,
    VRegType::mm4, VRegType::mm5, VRegType::mm6, VRegType::mm7};
constexpr u32 n_float_arg_regs = sizeof(int_arg_reg) / sizeof(int_arg_reg[0]);

static void save_regs_callee(MFunc &func, CFG &cfg) {
  auto &first_bb = func.bbs[0];
  auto used_regs = calculate_used_regs(func);
  // store all the regs in the initial bb
  size_t n_regs_saved = 0;
  for (auto reg_ty : callee_saved) {

    if (!used_regs[(u8)reg_ty - 1] || reg_ty == VRegType::SP ||
        reg_ty == VRegType::BP) {
      continue;
    }
    auto arg = MArgument{VReg{0, VRegInfo{reg_ty, Type::Int64}}, Type::Int64};
    first_bb.instrs.insert(first_bb.instrs.begin() + 0,
                           MInstr{Opcode::push, arg});
    n_regs_saved++;
  }

  // after we saved all regs now our argument loading is potentially fucked
  // since the argument loading is also handled by the cc we can just clean it
  // up here
  //  and assume this always works.

  for (size_t instr_id = n_regs_saved;
       instr_id < n_regs_saved + func.args.size(); instr_id++) {
    auto &instr = first_bb.instrs[instr_id];
    ASSERT(instr.op == Opcode::mov);
    ASSERT(instr.args[1].isReg() ||
           (instr.args[1].isMem() &&
            instr.args[1].type == MArgument::ArgumentType::MemImmVReg));
    // only update things given by stack
    if (instr.args[1].isMem()) {
      instr.args[1].imm += n_regs_saved * 8;
    }
  }

  // align the stack
  if (n_regs_saved % 2 != 0) {
    first_bb.instrs.insert(first_bb.instrs.begin() + 0,
                           MInstr{Opcode::sub2,
                                  MArgument{VReg::RSP(), Type::Int64},
                                  MArgument{8U}});
  }

  // restore in *every* exiting basic block we first need to find these
  // should be all cfg blocks without any successors??

  for (size_t bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
    if (cfg.bbrs[bb_id].succ.size() != 0) {
      continue;
    }

    size_t n_regs_restored = 0;
    for (auto reg_ty : callee_saved) {
      if (!used_regs[(u8)reg_ty - 1] || reg_ty == VRegType::SP ||
          reg_ty == VRegType::BP) {
        continue;
      }
      auto arg = MArgument{VReg{0, VRegInfo{reg_ty, Type::Int64}}, Type::Int64};
      func.bbs[bb_id].instrs.insert(func.bbs[bb_id].instrs.end() - 1,
                                    MInstr{Opcode::pop, arg});
      n_regs_restored++;
    }
    ASSERT(n_regs_saved == n_regs_restored);
    // align the stack
    if (n_regs_restored % 2 != 0) {
      func.bbs[bb_id].instrs.insert(func.bbs[bb_id].instrs.end() - 1,
                                    MInstr{Opcode::add2,
                                           MArgument{VReg::RSP(), Type::Int64},
                                           MArgument{8U}});
    }
  }
}

static bool is_alive(VReg reg_ty, TMap<VReg, LinearRangeSet> &lives,
                     size_t start, size_t end, size_t bb_id) {
  if (!lives.contains(reg_ty)) {
    return false;
  }
  return lives.at(reg_ty).collide(LinearRange::inBB(bb_id, start, end + 1));
}

static void save_locals(IRVec<MInstr> &instrs,
                        TMap<VReg, LinearRangeSet> &lives, size_t start,
                        size_t end, size_t bb_id, MInstr &call,
                        bool return_value_overwrites_ret_reg) {
  for (auto reg_ty : caller_saved | std::views::reverse) {
    if (call.n_args == 2 && return_value_overwrites_ret_reg) {
      bool is_float =
          call.args[1].ty == Type::Float32 || call.args[1].ty == Type::Float64;
      if (!is_float && reg_ty == VRegType::A) {
        continue;
      }
      if (is_float && reg_ty == VRegType::mm0) {
        continue;
      }
    }
    if (!is_alive(VReg{reg_ty}, lives, end, end, bb_id) ||
        reg_ty == VRegType::SP || reg_ty == VRegType::BP) {
      continue;
    }
    auto arg = MArgument{VReg{0, VRegInfo{reg_ty, Type::Int64}}, Type::Int64};
    instrs.insert(instrs.begin() + (i64)start, MInstr{Opcode::push, arg});
  }
}

static uint32_t restore_locals(IRVec<MInstr> &instrs,
                               TMap<VReg, LinearRangeSet> &lives, size_t start,
                               size_t end, size_t bb_id,
                               bool return_value_overwrites_ret_reg,
                               MInstr &call) {
  uint32_t n_locals_restored = 0;

  bool can_skip_a = false;
  bool can_skip_mm0 = false;
  // ret value
  if (call.n_args == 2) {
    auto ret_type = call.args[1].ty;
    bool is_float = ret_type == Type::Float32 || ret_type == Type::Float64;
    auto ret_reg_type = is_float ? VRegType::mm0 : VRegType::A;
    if (is_float) {
      can_skip_mm0 = true;
    } else {
      can_skip_a = true;
    }

    if (!return_value_overwrites_ret_reg) {
      bool a_gets_overwritten =
          (!is_float && is_alive(VReg{VRegType::A}, lives, start, end, bb_id));
      bool mm0_gets_overwritten =
          (is_float && is_alive(VReg{VRegType::mm0}, lives, start, end, bb_id));
      if (a_gets_overwritten || mm0_gets_overwritten) {
        auto arg = MArgument{VReg{0, VRegInfo{ret_reg_type, Type::Int64}},
                             Type::Int64};
        instrs.insert(instrs.begin() + (i64)start, MInstr{Opcode::pop, arg});
        n_locals_restored++;
      }
    }

    instrs.insert(
        instrs.begin() + (i64)start,
        MInstr{Opcode::mov, call.args[1],
               MArgument{VReg{0, VRegInfo{ret_reg_type, ret_type}}, ret_type}});
  }

  for (auto reg_ty : caller_saved) {
    bool skip_a = reg_ty == VRegType::A && can_skip_a;
    bool skip_mm0 = reg_ty == VRegType::mm0 && can_skip_mm0;
    if (!is_alive(VReg{reg_ty}, lives, end, end, bb_id) || skip_a ||
        skip_mm0 || reg_ty == VRegType::SP || reg_ty == VRegType::BP) {
      continue;
    }
    auto arg = MArgument{VReg{0, VRegInfo{reg_ty, Type::Int64}}, Type::Int64};
    instrs.insert(instrs.begin() + (i64)start, MInstr{Opcode::pop, arg});
    n_locals_restored++;
  }
  return n_locals_restored;
}

struct ArgPosition {
  enum Type {
    IntReg,
    FloatReg,
    Stack,
  };
  Type ty;
  u32 position;
};

u32 calculate_arg_locations(const TVec<MInstr> &args, TVec<ArgPosition> &pos) {
  u32 int_arg_id = 0;
  u32 float_arg_id = 0;
  u32 stack_arg_id = 0;
  for (const auto &arg : args) {
    const auto &arg_ty = arg.args[0].ty;
    bool is_float = arg_ty == Type::Float32 || arg_ty == Type::Float64;
    if (is_float && float_arg_id < n_float_arg_regs) {
      pos.push_back({ArgPosition::Type::FloatReg, float_arg_id});
      float_arg_id++;
    } else if (!is_float && int_arg_id < n_int_arg_regs) {
      pos.push_back({ArgPosition::Type::IntReg, int_arg_id});
      int_arg_id++;
    } else {
      pos.push_back({ArgPosition::Type::Stack, stack_arg_id});
      stack_arg_id++;
    }
  }
  return stack_arg_id;
}

void generate_arg(TVec<MInstr> &instrs, const MInstr &arg,
                  const ArgPosition &arg_pos) {
  const auto &arg_ty = arg.args[0].ty;
  switch (arg_pos.ty) {
  case ArgPosition::IntReg:
    instrs.emplace_back(
        Opcode::mov, MArgument{{int_arg_reg[arg_pos.position], arg_ty}, arg_ty},
        arg.args[0]);
    break;
  case ArgPosition::FloatReg:
    instrs.emplace_back(
        Opcode::mov,
        MArgument{{float_arg_reg[arg_pos.position], arg_ty}, arg_ty},
        arg.args[0]);
    break;
  case ArgPosition::Stack:
    instrs.emplace_back(Opcode::push, arg.args[0]);
    break;
  }
}

void setup_call_arguments(IRVec<MInstr> &out_instrs, const TVec<MInstr> &args,
                          const TVec<ArgPosition> &arg_pos, size_t start) {

  TVec<MInstr> output_vec;
  TVec<u32> worklist;
  for (size_t arg_id = 0; arg_id < args.size(); arg_id++) {
    if (arg_pos[arg_id].ty == ArgPosition::Type::Stack) {
      generate_arg(output_vec, args[arg_id], arg_pos[arg_id]);
    } else {
      worklist.push_back(arg_id);
    }
  }

  // queue for saving regs into push and then restoring them at the end
  TVec<u32> push_pop_queue{};
  while (!worklist.empty()) {
    bool found_one = false;
    for (size_t curr_work_item = 0; curr_work_item < worklist.size();
         curr_work_item++) {
      auto arg_id = worklist[curr_work_item];
      VRegType wants_reg = VRegType::A;
      switch (arg_pos[arg_id].ty) {
      case ArgPosition::IntReg:
        wants_reg = int_arg_reg[arg_pos[arg_id].position];
        break;
      case ArgPosition::FloatReg:
        wants_reg = float_arg_reg[arg_pos[arg_id].position];
        break;
      case ArgPosition::Stack:
        UNREACH();
        break;
      }

      bool collision = false;
      for (size_t other_id = 0; other_id < worklist.size(); other_id++) {
        if (other_id != curr_work_item &&
            args[worklist[other_id]].args[0].uses_same_vreg(wants_reg)) {
          collision = true;
          break;
        }
      }
      if (!collision) {
        generate_arg(output_vec, args[arg_id], arg_pos[arg_id]);
        worklist.erase(worklist.begin() + curr_work_item);
        found_one = true;
        break;
      }
    }
    // if we didnt find any that dont colllide
    // we us a push and pop
    if (!found_one && !worklist.empty()) {
      output_vec.emplace_back(Opcode::push, args[worklist[0]].args[0]);
      push_pop_queue.push_back(worklist[0]);
      worklist.erase(worklist.begin() + 0);
    }
  }

  for (auto push_pop : push_pop_queue | std::views::reverse) {
    auto &arg = args[push_pop];
    auto &arg_ty = arg.args[0].ty;
    auto arg_po = arg_pos[push_pop];
    switch (arg_po.ty) {
    case ArgPosition::IntReg:
      output_vec.emplace_back(
          Opcode::pop,
          MArgument{{int_arg_reg[arg_po.position], arg_ty}, arg_ty});
      break;
    case ArgPosition::FloatReg:
      output_vec.emplace_back(
          Opcode::pop,
          MArgument{{float_arg_reg[arg_po.position], arg_ty}, arg_ty});
      break;
    case ArgPosition::Stack:
      UNREACH();
    }
  }
  out_instrs.insert(out_instrs.begin() + start, output_vec.begin(),
                    output_vec.end());
}

static void transform_call(IRVec<MInstr> &instrs, size_t start, size_t end,
                           size_t bb_id, TMap<VReg, LinearRangeSet> &lives) {

  size_t n_args = end - start;

  TVec<MInstr> args;
  MInstr call = instrs[end];
  args.reserve(n_args);

  for (u32 i = 0; i < n_args; i++) {
    args.push_back(instrs.at(start + i));
  }
  instrs.erase(instrs.begin() + (i64)start, instrs.begin() + (i64)end + 1);

  bool return_value_overwrites_ret_reg =
      (call.args[1].isReg() && (call.args[1].reg.info.ty == VRegType::A ||
                                call.args[1].reg.info.ty == VRegType::mm0));

  const uint32_t n_locals_need_saving = restore_locals(
      instrs, lives, start, end, bb_id, return_value_overwrites_ret_reg, call);

  TVec<ArgPosition> arg_pos;
  auto n_stack_args = calculate_arg_locations(args, arg_pos);

  if ((n_locals_need_saving + n_stack_args) % 2 != 0) {
    instrs.insert(instrs.begin() + (i64)start + (i64)n_locals_need_saving +
                      (call.n_args == 2 ? 1 : 0),
                  MInstr{Opcode::add2, MArgument{VReg::RSP(), Type::Int64},
                         MArgument{8U}});
  }

  // cleanup args
  {
    auto sp = MArgument{VReg::RSP(), Type::Int64};
    if (n_stack_args > 0) {
      instrs.insert(instrs.begin() + (i64)start,
                    MInstr{Opcode::add2, sp, 8 * n_stack_args});
    }
  }

  // do call
  instrs.insert(instrs.begin() + (i64)start,
                MInstr{Opcode::call, call.args[0]});
  // setup args
  setup_call_arguments(instrs, args, arg_pos, start);
  // for (auto &arg : args) {
  //   instrs.insert(instrs.begin() + (i64)start,
  //                 MInstr{Opcode::push, arg.args[0]});
  // }
  // save locals
  save_locals(instrs, lives, start, end, bb_id, call,
              return_value_overwrites_ret_reg);

  if ((n_locals_need_saving + n_stack_args) % 2 != 0) {
    instrs.insert(instrs.begin() + (i64)start,
                  MInstr{Opcode::sub2, MArgument{VReg::RSP(), Type::Int64},
                         MArgument{8U}});
  }
}

static_assert((u8)VRegType::R15 == 16);
static_assert((u8)VRegType::A == 1);

utils::BitSet<> calculate_used_regs(const MFunc &f) {
  utils::BitSet<> res{(u8)VRegType::N_REGS, false};
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
        case MArgument::ArgumentType::MemImmVRegVRegScale:
        case MArgument::ArgumentType::MemVRegVRegScale: {
          ASSERT(arg.reg.info.ty != VRegType::Virtual);
          ASSERT(arg.indx.info.ty != VRegType::Virtual);
          res[(u8)arg.reg.info.ty - 1].set(true);
          res[(u8)arg.indx.info.ty - 1].set(true);
          break;
        }
        case MArgument::ArgumentType::MemImmVRegScale: {
          ASSERT(arg.indx.info.ty != VRegType::Virtual);
          res[(u8)arg.indx.info.ty - 1].set(true);
          break;
        }
        }
      }
    }
  }

  u32 int_arg_id = 0;
  u32 float_arg_id = 0;
  for (u32 arg_i = 0; arg_i < f.args.size(); arg_i++) {
    auto arg_ty = f.arg_tys[arg_i];
    // register saving in this cc
    bool is_float = arg_ty == Type::Float32 || arg_ty == Type::Float64;
    if (is_float && float_arg_id < n_float_arg_regs) {
      res[(u8)float_arg_reg[float_arg_id] - 1].set(true);
      float_arg_id++;
    } else if (!is_float && int_arg_id < n_int_arg_regs) {
      res[(u8)int_arg_reg[int_arg_id] - 1].set(true);
      int_arg_id++;
    }
  }

  return res;
}

void CallingConv::second_stage(FVec<MFunc> &funcs) {
  ZoneScopedN("CC 2nd Stage");
  for (auto &func : funcs) {
    CFG cfg(func);

    save_regs_callee(func, cfg);

    TMap<VReg, LinearRangeSet> lives = linear_lifetime(func);

    size_t bb_id = 0;
    for (auto &bb : func.bbs) {
      size_t n_instrs = bb.instrs.size();
      for (size_t instr_idp1 = n_instrs; instr_idp1 > 0; instr_idp1--) {
        size_t instr_end_id = instr_idp1 - 1;
        if (bb.instrs[instr_end_id].op != Opcode::invoke) {
          continue;
        }
        size_t instr_start_idp1 = instr_end_id + 1;
        size_t instr_start_id = instr_end_id;
        for (instr_start_idp1 = instr_end_id; instr_start_idp1 > 0;
             instr_start_idp1--) {
          instr_start_id = instr_start_idp1 - 1;
          if (bb.instrs[instr_start_id].op != Opcode::arg_setup) {
            instr_start_id++;
            break;
          }
        }
        transform_call(bb.instrs, instr_start_id, instr_end_id, bb_id, lives);
        // update the n of instrs since the might have changed it
        n_instrs = bb.instrs.size();
      }
      // for (size_t instr_id = 0; instr_id < n_instrs; instr_id++) {
      //   if (bb.instrs[instr_id].op != Opcode::arg_setup &&
      //       bb.instrs[instr_id].op != Opcode::invoke) {
      //     continue;
      //   }
      //   for (size_t instr_end_id = instr_id; instr_end_id < n_instrs;
      //        instr_end_id++) {
      //     if (bb.instrs[instr_end_id].op != Opcode::invoke) {
      //       continue;
      //     }
      //     // FIXME: needs proper liveness analysis
      //     transform_call(bb.instrs, instr_id, instr_end_id, bb_id, lives);
      //     // update the n of instrs since the might have changed it
      //     n_instrs = bb.instrs.size();
      //     // number of elements
      //     // instr_id = 0;
      //     break;
      //   }
      // }
      bb_id++;
    }
  }
}

void gen_arg_mapping(MFunc &func) {
  u32 int_arg_id = 0;
  u32 float_arg_id = 0;
  u32 n_stack_args = 0;

  (void)float_arg_reg;
  (void)int_arg_reg;
  for (u32 arg_i = 0; arg_i < func.args.size(); arg_i++) {
    ASSERT(!func.args[arg_i].info.is_pinned());
    auto arg_ty = func.arg_tys[arg_i];
    // this needs to stay this way or needs to be synched with the callee
    // register saving in this cc
    MInstr instr{Opcode::mov};
    bool is_float = arg_ty == Type::Float32 || arg_ty == Type::Float64;
    if (is_float && float_arg_id < n_float_arg_regs) {
      instr = MInstr(Opcode::mov, MArgument{func.args[arg_i], arg_ty},
                     MArgument{{float_arg_reg[float_arg_id], arg_ty}, arg_ty});
      // func.args[arg_i].info = VRegInfo{float_arg_reg[float_arg_id], arg_ty};
      float_arg_id++;
    } else if (!is_float && int_arg_id < n_int_arg_regs) {
      instr = MInstr(Opcode::mov, MArgument{func.args[arg_i], arg_ty},
                     MArgument{{int_arg_reg[int_arg_id], arg_ty}, arg_ty});
      // func.args[arg_i].info = VRegInfo{int_arg_reg[int_arg_id], arg_ty};
      int_arg_id++;
    } else {
      instr =
          MInstr(Opcode::mov, MArgument{func.args[arg_i], arg_ty},
                 MArgument::Mem(VReg::RSP(), 8 * (n_stack_args + 2), arg_ty));
      n_stack_args++;
    }
    func.bbs[0].instrs.insert(func.bbs[0].instrs.begin(), instr);
  }
}

static void function_argument_loading(MFunc &func) { gen_arg_mapping(func); }

void mark_arguments_with_regs(IRVec<MInstr> &instrs, size_t instr_start_id,
                              size_t instr_end_id) {
  TVec<ArgPosition> pos;
  size_t n_args = instr_end_id - instr_start_id;

  TVec<MInstr> args;
  args.reserve(n_args);

  for (u32 i = 0; i < n_args; i++) {
    args.push_back(instrs.at(instr_start_id + i));
  }

  calculate_arg_locations(args, pos);

  for (u32 i = 0; i < n_args; i++) {
    auto arg_pos = pos[i];
    auto arg_ty = instrs[instr_start_id + i].args[0].ty;
    switch (arg_pos.ty) {
    case ArgPosition::IntReg:
      instrs[instr_start_id + i].n_args = 2;
      instrs[instr_start_id + i].args[1] =
          MArgument({int_arg_reg[arg_pos.position], arg_ty}, arg_ty);
      break;
    case ArgPosition::FloatReg:
      instrs[instr_start_id + i].n_args = 2;
      instrs[instr_start_id + i].args[1] =
          MArgument({float_arg_reg[arg_pos.position], arg_ty}, arg_ty);
      break;
    case ArgPosition::Stack:
      break;
    }
  }
}

void CallingConv::first_stage(FVec<MFunc> &funcs) {
  ZoneScopedN("CC 1st Stage");
  for (auto &func : funcs) {
    function_argument_loading(func);
  }
  for (auto &func : funcs) {
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
          mark_arguments_with_regs(bb.instrs, instr_id, instr_end_id);
          instr_id = instr_end_id;
          break;
        }
      }
    }
  }
}

} // namespace foptim::fmir
