#include "calling_conv.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"
#include "utils/bitset.hpp"
#include "utils/todo.hpp"
#include <ranges>

namespace foptim::fmir {

utils::BitSet<> calculate_used_regs(const MFunc &f);

constexpr CReg caller_saved[] = {
    CReg::A,    CReg::C,    CReg::D,    CReg::SI,   CReg::DI,
    CReg::R8,   CReg::R9,   CReg::R10,  CReg::R11,  CReg::mm0,
    CReg::mm1,  CReg::mm2,  CReg::mm3,  CReg::mm4,  CReg::mm5,
    CReg::mm6,  CReg::mm7,  CReg::mm8,  CReg::mm9,  CReg::mm10,
    CReg::mm11, CReg::mm12, CReg::mm13, CReg::mm14, CReg::mm15};

constexpr CReg callee_saved[] = {
    CReg::R12, CReg::R13, CReg::R14, CReg::R15, CReg::B, CReg::SP, CReg::BP,
};

constexpr CReg int_arg_reg[] = {
    CReg::DI, CReg::SI, CReg::D, CReg::C, CReg::R8, CReg::R9,
};
constexpr u32 n_int_arg_regs = sizeof(int_arg_reg) / sizeof(int_arg_reg[0]);
constexpr CReg float_arg_reg[] = {CReg::mm0, CReg::mm1, CReg::mm2, CReg::mm3,
                                  CReg::mm4, CReg::mm5, CReg::mm6, CReg::mm7};
constexpr u32 n_float_arg_regs = sizeof(int_arg_reg) / sizeof(int_arg_reg[0]);

static void save_regs_callee(MFunc &func, CFG &cfg) {
  auto &first_bb = func.bbs[0];

  auto used_regs = calculate_used_regs(func);
  // store all the regs in the initial bb
  size_t n_regs_saved = 0;
  for (auto reg_ty : callee_saved) {

    if (!used_regs[(u8)reg_ty - 1] || reg_ty == CReg::SP ||
        reg_ty == CReg::BP) {
      continue;
    }
    auto arg = MArgument{VReg{reg_ty, Type::Int64}, Type::Int64};
    first_bb.instrs.insert(first_bb.instrs.begin() + 0,
                           MInstr{Opcode::push, arg});
    n_regs_saved++;
  }

  // after we saved all regs now our argument loading is potentially fucked
  // since the argument loading is also handled by the cc we can just clean it
  // up here
  //  and assume this always works.

  // for (size_t instr_id = n_regs_saved;
  //      instr_id < n_regs_saved + func.args.size(); instr_id++) {
  //   auto &instr = first_bb.instrs[instr_id];
  //   ASSERT(instr.op == Opcode::mov);
  //   ASSERT(instr.args[1].isReg() ||
  //          (instr.args[1].isMem() &&
  //           instr.args[1].type == MArgument::ArgumentType::MemImmVReg));
  //   // only update things given by stack
  //   if (instr.args[1].isMem()) {
  //     instr.args[1].imm += n_regs_saved * 8;
  //   }
  // }

  // align the stack
  if (n_regs_saved % 2 != 0) {
    first_bb.instrs.insert(first_bb.instrs.begin() + 0,
                           MInstr{Opcode::sub2,
                                  MArgument{VReg::RSP(), Type::Int64},
                                  MArgument{8U}});
  }

  // save all potential va args from registers into the register_save_area
  if (func.needs_register_save_area) {
    std::pair<VReg, u32> arg_int_regs[6] = {
        {VReg::RDI(), 0},  {VReg::RSI(), 8}, {VReg::RDX(), 16},
        {VReg::RCX(), 24}, {VReg::R8(), 32}, {VReg::R9(), 40},
    };
    std::pair<VReg, u32> arg_xmm_regs[8] = {
        {VReg{CReg::mm0, Type::Float64}, 48},
        {VReg{CReg::mm1, Type::Float64}, 64},
        {VReg{CReg::mm2, Type::Float64}, 80},
        {VReg{CReg::mm3, Type::Float64}, 96},
        {VReg{CReg::mm4, Type::Float64}, 112},
        {VReg{CReg::mm5, Type::Float64}, 128},
        {VReg{CReg::mm6, Type::Float64}, 144},
        {VReg{CReg::mm7, Type::Float64}, 160},
        // {VReg{CReg::mm8, Type::Float64}, 176},
        // {VReg{CReg::mm9, Type::Float64}, 192},
        // {VReg{CReg::mm10, Type::Float64}, 208},
        // {VReg{CReg::mm11, Type::Float64}, 224},
        // {VReg{CReg::mm12, Type::Float64}, 240},
        // {VReg{CReg::mm13, Type::Float64}, 256},
        // {VReg{CReg::mm14, Type::Float64}, 272},
        // {VReg{CReg::mm15, Type::Float64}, 288},
    };

    for (auto [reg, offset] : arg_int_regs) {
      first_bb.instrs.insert(
          first_bb.instrs.begin() + 0,
          MInstr{Opcode::mov,
                 MArgument::MemOB((i32)offset, VReg::RSP(), Type::Int64),
                 MArgument{reg, reg.ty}});
    }

    // TODO: make this conitional based if there are xmm args read out of al
    // (see clang/godbolt)
    //  https://godbolt.org/#g:!((g:!((g:!((h:codeEditor,i:(filename:'1',fontScale:15,fontUsePx:'0',j:1,lang:llvm,selection:(endColumn:34,endLineNumber:19,positionColumn:1,positionLineNumber:19,selectionStartColumn:34,selectionStartLineNumber:19,startColumn:1,startLineNumber:19),source:'%3B+For+Unix+x86_64+platforms,+va_list+is+the+following+struct:%0A%25struct.va_list+%3D+type+%7B+i32,+i32,+ptr,+ptr+%7D%0A%0Adefine+i32+@test(i32+%25x,+...)+%7B%0A++%3B+Initialize+variable+argument+processing%0A++%25ap+%3D+alloca+%25struct.va_list%0A++call+void+@llvm.va_start.p0(ptr+%25ap)%0A%0A++%3B+Read+a+single+integer+argument%0A++%3B%25tmp+%3D+va_arg+ptr+%25ap,+i32%0A%0A++%3B+Stop+processing+of+arguments.%0A++%3Bcall+void+@llvm.va_end.p0(ptr+%25ap)%0A++ret+i32+0%0A%7D%0A%0Adeclare+void+@llvm.va_start.p0(ptr)%0Adeclare+void+@llvm.va_copy.p0(ptr,+ptr)%0Adeclare+void+@llvm.va_end.p0(ptr)'),l:'5',n:'0',o:'LLVM+IR+source+%231',t:'0')),k:44.60580912863071,l:'4',n:'0',o:'',s:0,t:'0'),(g:!((h:compiler,i:(compiler:llctrunk,filters:(b:'0',binary:'1',binaryObject:'1',commentOnly:'0',debugCalls:'0',demangle:'0',directives:'0',execute:'1',intel:'0',libraryCode:'0',trim:'0',verboseDemangling:'0'),flagsViewOpen:'1',fontScale:18,fontUsePx:'0',j:1,lang:llvm,libs:!(),options:'',overrides:!(),selection:(endColumn:13,endLineNumber:4,positionColumn:1,positionLineNumber:3,selectionStartColumn:13,selectionStartLineNumber:4,startColumn:1,startLineNumber:3),source:1),l:'5',n:'0',o:'+llc+(trunk)+(Editor+%231)',t:'0')),k:55.39419087136929,l:'4',n:'0',o:'',s:0,t:'0')),l:'2',n:'0',o:'',t:'0')),version:4
    // TODO: should also investigate why it doesnt save xmm8-xmm15 and not RDI
    for (auto [reg, offset] : arg_xmm_regs) {
      first_bb.instrs.insert(
          first_bb.instrs.begin() + 0,
          MInstr{Opcode::mov,
                 MArgument::MemOB((i32)offset, VReg::RSP(), Type::Int64),
                 MArgument{reg, reg.ty}});
    }
    first_bb.instrs.insert(first_bb.instrs.begin() + 0,
                           MInstr{Opcode::sub2,
                                  MArgument{VReg::RSP(), Type::Int64},
                                  MArgument{176U}});
  }

  // restore in *every* exiting basic block we first need to find these
  // should be all cfg blocks without any successors??

  for (size_t bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
    if (cfg.bbrs[bb_id].succ.size() != 0) {
      continue;
    }

    size_t n_regs_restored = 0;
    for (auto reg_ty : callee_saved) {
      if (!used_regs[(u8)reg_ty - 1] || reg_ty == CReg::SP ||
          reg_ty == CReg::BP) {
        continue;
      }
      auto arg = MArgument{VReg{reg_ty, Type::Int64}, Type::Int64};
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
    if (func.needs_register_save_area) {
      func.bbs[bb_id].instrs.insert(func.bbs[bb_id].instrs.end() - 1,
                                    MInstr{Opcode::add2,
                                           MArgument{VReg::RSP(), Type::Int64},
                                           MArgument{176U}});
      fmt::println("====VA====\n{}", func);
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
    if (call.n_args == 2) {
      bool is_float = call.args[1].is_fp();
      if (!is_float && reg_ty == CReg::A) {
        continue;
      }
      if (is_float && reg_ty == CReg::mm0) {
        continue;
      }
    }
    if (!is_alive(VReg{reg_ty}, lives, end, end, bb_id) || reg_ty == CReg::SP ||
        reg_ty == CReg::BP) {
      continue;
    }
    auto arg = MArgument{VReg{reg_ty, Type::Int64}, Type::Int64};
    instrs.insert(instrs.begin() + (i64)start, MInstr{Opcode::push, arg});
  }
  if (call.n_args == 2 && !return_value_overwrites_ret_reg) {
    const auto reg_ty = call.args[1].is_fp() ? CReg::mm0 : CReg::A;
    if (is_alive(VReg{reg_ty}, lives, end, end, bb_id)) {
      auto arg = MArgument{VReg{reg_ty, Type::Int64}, Type::Int64};
      instrs.insert(instrs.begin() + (i64)start, MInstr{Opcode::push, arg});
    }
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
    bool is_float = call.args[1].is_fp();
    auto ret_reg_type = is_float ? CReg::mm0 : CReg::A;
    if (is_float) {
      can_skip_mm0 = true;
    } else {
      can_skip_a = true;
    }

    if (!return_value_overwrites_ret_reg) {
      bool a_gets_overwritten =
          (!is_float && is_alive(VReg{CReg::A}, lives, end, end, bb_id));
      bool mm0_gets_overwritten =
          (is_float && is_alive(VReg{CReg::mm0}, lives, end, end, bb_id));
      if (a_gets_overwritten || mm0_gets_overwritten) {
        auto arg = MArgument{VReg{ret_reg_type, Type::Int64}, Type::Int64};
        instrs.insert(instrs.begin() + (i64)start, MInstr{Opcode::pop, arg});
        n_locals_restored++;
      }
    }

    auto ret_type = call.args[1].ty;
    instrs.insert(instrs.begin() + (i64)start,
                  MInstr{Opcode::mov, call.args[1],
                         MArgument{VReg{ret_reg_type, ret_type}, ret_type}});
  }

  for (auto reg_ty : caller_saved) {
    bool skip_a = reg_ty == CReg::A && can_skip_a;
    bool skip_mm0 = reg_ty == CReg::mm0 && can_skip_mm0;
    if (!is_alive(VReg{reg_ty}, lives, end, end, bb_id) || skip_a || skip_mm0 ||
        reg_ty == CReg::SP || reg_ty == CReg::BP) {
      continue;
    }
    auto arg = MArgument{VReg{reg_ty, Type::Int64}, Type::Int64};
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
      CReg wants_reg = CReg::A;
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
      (call.args[1].isReg() && (call.args[1].reg.c_reg() == CReg::A ||
                                call.args[1].reg.c_reg() == CReg::mm0));

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

static_assert((u8)CReg::R15 == 16);
static_assert((u8)CReg::A == 1);

utils::BitSet<> calculate_used_regs(const MFunc &f) {
  utils::BitSet<> res{(u8)CReg::N_REGS, false};
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
          res[(u8)arg.reg.c_reg() - 1].set(true);
          break;
        }
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVRegScale:
        case MArgument::ArgumentType::MemVRegVRegScale: {
          res[(u8)arg.reg.c_reg() - 1].set(true);
          res[(u8)arg.indx.c_reg() - 1].set(true);
          break;
        }
        case MArgument::ArgumentType::MemImmVRegScale: {
          res[(u8)arg.indx.c_reg() - 1].set(true);
          break;
        }
        }
      }
    }
  }

  u32 int_arg_id = 0;
  u32 float_arg_id = 0;
  for (u32 arg_i = 0; arg_i < f.args.size(); arg_i++) {
    auto arg_ty = f.args[arg_i].ty;
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
    // fmt::println("\n=========\n{}", func);
    // for (auto &[reg, ranges] : lives) {
    //   fmt::print("  {}  ", reg);
    //   ranges.dump();
    // }

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
    ASSERT(!func.args[arg_i].is_concrete());
    auto arg_ty = func.args[arg_i].ty;
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
                 MArgument::MemOB(8 * (n_stack_args + 2), VReg::RSP(), arg_ty));
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
