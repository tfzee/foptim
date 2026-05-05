#include "calling_conv2.hpp"

#include "mir/analysis/cfg.hpp"
#include "mir/analysis/live_variables.hpp"

namespace foptim::fmir {
// ###################################################################################
//  First Stage

namespace {
struct ArgPosition {
  enum Type {
    IntReg,
    FloatReg,
    Stack,
  };
  Type ty;
  u32 position;
};

void gen_arg_mapping(MFunc &func, const CallingConvDefinition &conv) {
  u64 n_int_arg_regs = conv.args.gpr.size();
  u64 n_float_arg_regs = conv.args.fvr.size();

  u32 int_arg_id = 0;
  u32 float_arg_id = 0;
  u32 n_stack_args = 0;

  for (u32 arg_i = 0; arg_i < func.args.size(); arg_i++) {
    ASSERT(!func.args[arg_i].is_concrete());
    auto arg_ty = func.args[arg_i].ty;
    MInstr instr{GBaseSubtype::mov};
    bool is_vec_reg = arg_ty >= Type::Float32;
    if (is_vec_reg && float_arg_id < n_float_arg_regs) {
      instr = MInstr(GBaseSubtype::mov, MArgument{func.args[arg_i], arg_ty},
                     MArgument{{conv.args.fvr[float_arg_id], arg_ty}, arg_ty});
      float_arg_id++;
    } else if (!is_vec_reg && int_arg_id < n_int_arg_regs) {
      instr = MInstr(GBaseSubtype::mov, MArgument{func.args[arg_i], arg_ty},
                     MArgument{{conv.args.gpr[int_arg_id], arg_ty}, arg_ty});
      int_arg_id++;
    } else {
      ASSERT(conv.allows_stack_arg_spilling);
      instr = MInstr(GBaseSubtype::stack_arg_load,
                     MArgument{func.args[arg_i], arg_ty},
                     MArgument::MemOB(8 * (n_stack_args), VReg::RSP(), arg_ty));
      n_stack_args++;
    }
    func.bbs[0].instrs.insert(func.bbs[0].instrs.begin(), instr);
  }
}

u32 calculate_arg_locations(const TVec<MInstr> &args,
                            const CallingConvDefinition &conv,
                            TVec<ArgPosition> &pos) {
  u64 n_int_arg_regs = conv.args.gpr.size();
  u64 n_float_arg_regs = conv.args.fvr.size();
  u32 int_arg_id = 0;
  u32 float_arg_id = 0;
  u32 stack_arg_id = 0;
  for (const auto &arg : args) {
    const auto &arg_ty = arg.args[0].ty;
    bool is_vec_reg = arg_ty >= Type::Float32;
    if (is_vec_reg && float_arg_id < n_float_arg_regs) {
      pos.push_back({ArgPosition::Type::FloatReg, float_arg_id});
      float_arg_id++;
    } else if (!is_vec_reg && int_arg_id < n_int_arg_regs) {
      pos.push_back({ArgPosition::Type::IntReg, int_arg_id});
      int_arg_id++;
    } else {
      pos.push_back({ArgPosition::Type::Stack, stack_arg_id});
      stack_arg_id++;
    }
  }
  return stack_arg_id;
}

void mark_arguments_with_regs(IRVec<MInstr> &instrs,
                              const CallingConvDefinition &conv,
                              size_t instr_start_id, size_t instr_end_id) {
  TVec<ArgPosition> pos;
  size_t n_args = instr_end_id - instr_start_id;

  TVec<MInstr> args;
  args.reserve(n_args);

  for (u32 i = 0; i < n_args; i++) {
    args.push_back(instrs.at(instr_start_id + i));
  }

  calculate_arg_locations(args, conv, pos);

  for (u32 i = 0; i < n_args; i++) {
    auto arg_pos = pos[i];
    auto arg_ty = instrs[instr_start_id + i].args[0].ty;
    switch (arg_pos.ty) {
      case ArgPosition::IntReg:
        instrs[instr_start_id + i].n_args = 2;
        instrs[instr_start_id + i].args[1] =
            MArgument({conv.args.gpr[arg_pos.position], arg_ty}, arg_ty);
        break;
      case ArgPosition::FloatReg:
        instrs[instr_start_id + i].n_args = 2;
        instrs[instr_start_id + i].args[1] =
            MArgument({conv.args.fvr[arg_pos.position], arg_ty}, arg_ty);
        break;
      case ArgPosition::Stack:
        break;
    }
  }
}

}  // namespace

void CallingConvImpl::first_stage(MFunc &func,
                                  const CallingConvDefinition &conv,
                                  const conf::CompConf &) {
  gen_arg_mapping(func, conv);
  for (auto &bb : func.bbs) {
    size_t n_instrs = bb.instrs.size();
    for (size_t instr_id = 0; instr_id < n_instrs; instr_id++) {
      if (!bb.instrs[instr_id].is(GBaseSubtype::arg_setup) &&
          !bb.instrs[instr_id].is(GBaseSubtype::invoke)) {
        continue;
      }
      for (size_t instr_end_id = instr_id; instr_end_id < n_instrs;
           instr_end_id++) {
        if (!bb.instrs[instr_end_id].is(GBaseSubtype::invoke)) {
          continue;
        }
        mark_arguments_with_regs(bb.instrs, conv, instr_id, instr_end_id);
        instr_id = instr_end_id;
        break;
      }
    }
  }
}

// ###################################################################################
//  Second Stage

namespace {
utils::BitSet<> calculate_used_regs(const MFunc &f,
                                    const CallingConvDefinition &cc) {
  u64 n_int_arg_regs = cc.args.gpr.size();
  u64 n_float_arg_regs = cc.args.fvr.size();
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
    bool is_vec_reg = arg_ty >= Type::Float32;
    if (is_vec_reg && float_arg_id < n_float_arg_regs) {
      res[(u8)cc.args.fvr[float_arg_id] - 1].set(true);
      float_arg_id++;
    } else if (!is_vec_reg && int_arg_id < n_int_arg_regs) {
      res[(u8)cc.args.gpr[int_arg_id] - 1].set(true);
      int_arg_id++;
    }
  }

  return res;
}

void save_regs_callee(MFunc &func, const CallingConvDefinition &cc, CFG &cfg) {
  auto &first_bb = func.bbs[0];

  auto used_regs = calculate_used_regs(func, cc);
  // store all the regs in the initial bb
  size_t n_regs_saved = 0;
  for (auto reg_ty : cc.callee_saved) {
    if (!used_regs[(u8)reg_ty - 1] || reg_ty == CReg::SP ||
        reg_ty == CReg::BP) {
      continue;
    }
    auto arg = MArgument{VReg{reg_ty, Type::Int64}, Type::Int64};
    first_bb.instrs.insert(first_bb.instrs.begin() + 0,
                           MInstr{GBaseSubtype::push, arg});
    n_regs_saved++;
  }

  // align the stack
  TODO("use corr allign");
  if (n_regs_saved % 2 != 0) {
    first_bb.instrs.insert(
        first_bb.instrs.begin() + 0,
        MInstr{GArithSubtype::sub2, MArgument{VReg::RSP(), Type::Int64},
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
          MInstr{GBaseSubtype::mov,
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
          MInstr{GBaseSubtype::mov,
                 MArgument::MemOB((i32)offset, VReg::RSP(), Type::Int64),
                 MArgument{reg, reg.ty}});
    }
    first_bb.instrs.insert(
        first_bb.instrs.begin() + 0,
        MInstr{GArithSubtype::sub2, MArgument{VReg::RSP(), Type::Int64},
               MArgument{176U}});
  }
  // after we push poped stuff to save em we then need to updated our stack
  // arguments so we actually use the right offsets.
  u32 additional_offset = 8 * (2 + n_regs_saved + n_regs_saved % 2) +
                          (func.needs_register_save_area ? 176 : 0);
  // NOTE: Assuming we got a full pro/epilogue because we reference SP

  for (auto &instr : first_bb.instrs) {
    if (instr.is(GBaseSubtype::stack_arg_load)) {
      instr.sop = (u32)GBaseSubtype::mov;
      instr.args[1].imm += additional_offset;
    }
  }

  // restore in *every* exiting basic block we first need to find these
  // should be all cfg blocks without any successors??

  for (size_t bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
    if (cfg.bbrs[bb_id].succ.size() != 0) {
      continue;
    }

    size_t n_regs_restored = 0;
    for (auto reg_ty : cc.callee_saved) {
      if (!used_regs[(u8)reg_ty - 1] || reg_ty == CReg::SP ||
          reg_ty == CReg::BP) {
        continue;
      }
      auto arg = MArgument{VReg{reg_ty, Type::Int64}, Type::Int64};
      func.bbs[bb_id].instrs.insert(func.bbs[bb_id].instrs.end() - 1,
                                    MInstr{GBaseSubtype::pop, arg});
      n_regs_restored++;
    }
    ASSERT(n_regs_saved == n_regs_restored);
    // align the stack
    TODO("use corr allign");
    if (n_regs_restored % 2 != 0) {
      func.bbs[bb_id].instrs.insert(
          func.bbs[bb_id].instrs.end() - 1,
          MInstr{GArithSubtype::add2, MArgument{VReg::RSP(), Type::Int64},
                 MArgument{8U}});
    }
    if (func.needs_register_save_area) {
      func.bbs[bb_id].instrs.insert(
          func.bbs[bb_id].instrs.end() - 1,
          MInstr{GArithSubtype::add2, MArgument{VReg::RSP(), Type::Int64},
                 MArgument{176U}});
    }
  }
}

static_assert((u8)CReg::R15 == 16);
static_assert((u8)CReg::A == 1);
TMap<CReg, Type> compute_max_reg_types(const MFunc &f) {
  TMap<CReg, Type> max_types;
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
            if (arg.reg.is_concrete()) {
              auto creg = arg.reg.c_reg();
              auto it = max_types.find(creg);
              if (it == max_types.end() || arg.ty > it->second) {
                max_types[creg] = arg.ty;
              }
            }
            break;
          }
          case MArgument::ArgumentType::MemVRegVReg:
          case MArgument::ArgumentType::MemImmVRegVReg:
          case MArgument::ArgumentType::MemImmVRegVRegScale:
          case MArgument::ArgumentType::MemVRegVRegScale: {
            if (arg.reg.is_concrete()) {
              auto creg = arg.reg.c_reg();
              auto it = max_types.find(creg);
              if (it == max_types.end() || arg.ty > it->second) {
                max_types[creg] = arg.ty;
              }
            }
            if (arg.indx.is_concrete()) {
              auto creg = arg.indx.c_reg();
              auto it = max_types.find(creg);
              if (it == max_types.end() || arg.ty > it->second) {
                max_types[creg] = arg.ty;
              }
            }
            break;
          }
          case MArgument::ArgumentType::MemImmVRegScale: {
            if (arg.indx.is_concrete()) {
              auto creg = arg.indx.c_reg();
              auto it = max_types.find(creg);
              if (it == max_types.end() || arg.ty > it->second) {
                max_types[creg] = arg.ty;
              }
            }
            break;
          }
        }
      }
    }
  }
  return max_types;
}

void transform_call(IRVec<MInstr> &instrs, const CallingConvDefinition &cc,
                    size_t start, size_t end, size_t bb_id,
                    TMap<VReg, LinearRangeSet> &lives,
                    const TMap<CReg, Type> &max_types) {
  size_t n_args = end - start;

  TVec<MInstr> args;
  MInstr call = instrs[end];
  auto is_var_args = call.is_var_arg_call;
  args.reserve(n_args);

  for (u32 i = 0; i < n_args; i++) {
    args.push_back(instrs.at(start + i));
  }
  instrs.erase(instrs.begin() + (i64)start, instrs.begin() + (i64)end + 1);

  bool return_value_overwrites_ret_reg = false;
  if (call.n_args > 1 && call.args[1].isReg()) {
    TODO("use da right ones");
    return_value_overwrites_ret_reg = (call.args[1].reg.c_reg() == CReg::A ||
                                       call.args[1].reg.c_reg() == CReg::mm0);
    if (call.n_args > 2 && call.args[2].isReg()) {
      return_value_overwrites_ret_reg &=
          (call.args[2].reg.c_reg() == CReg::D ||
           call.args[2].reg.c_reg() == CReg::mm1);
    }
  }

  const auto [n_locals_saved, n_local_bytes_need_saving] =
      restore_locals(instrs, lives, start, end, bb_id,
                     return_value_overwrites_ret_reg, call, max_types);

  TVec<ArgPosition> arg_pos;
  auto n_stack_args = calculate_arg_locations(args, cc, arg_pos);

  if ((n_local_bytes_need_saving + n_stack_args) % 2 != 0) {
    instrs.insert(instrs.begin() + (i64)start + (i64)n_locals_saved,
                  MInstr{GArithSubtype::add2,
                         MArgument{VReg::RSP(), Type::Int64}, MArgument{8U}});
  }

  // cleanup args
  {
    auto sp = MArgument{VReg::RSP(), Type::Int64};
    if (n_stack_args > 0) {
      instrs.insert(instrs.begin() + (i64)start,
                    MInstr{GArithSubtype::add2, sp, 8 * n_stack_args});
    }
  }

  // do call
  TODO("use link reg");
  instrs.insert(instrs.begin() + (i64)start,
                MInstr{GBaseSubtype::call, call.args[0]});
  instrs[start].is_var_arg_call = is_var_args;

  // for vararg setup al with the number of xmm registiers
  if (is_var_args) {
    u8 n_xmm_regs = 0;
    for (auto arg_p : arg_pos) {
      if (arg_p.ty == ArgPosition::FloatReg) {
        n_xmm_regs++;
      }
    }
    TODO("use var arg setup");
    instrs.insert(instrs.begin() + (i64)start,
                  MInstr{GBaseSubtype::mov,
                         MArgument{VReg{CReg::A, Type::Int8}, Type::Int8},
                         MArgument{(n_xmm_regs)}});
  }

  // setup args
  setup_call_arguments(instrs, args, arg_pos, start);
  // save locals
  save_locals(instrs, lives, start, end, bb_id, call,
              return_value_overwrites_ret_reg, max_types);

  if ((n_local_bytes_need_saving + n_stack_args) % 2 != 0) {
    instrs.insert(instrs.begin() + (i64)start,
                  MInstr{GArithSubtype::sub2,
                         MArgument{VReg::RSP(), Type::Int64}, MArgument{8U}});
  }
}

}  // namespace

void CallingConvImpl::second_stage(MFunc &func, const CallingConvDefinition &cc,
                                   const conf::CompConf &) {
  ASSERT(!func.variadic || cc.var_arg.supported);
  CFG cfg(func);
  save_regs_callee(func, cc, cfg);
  TMap<VReg, LinearRangeSet> lives = linear_lifetime(func);
  TMap<CReg, Type> max_types = compute_max_reg_types(func);

  size_t bb_id = 0;
  for (auto &bb : func.bbs) {
    size_t n_instrs = bb.instrs.size();
    for (size_t instr_idp1 = n_instrs; instr_idp1 > 0; instr_idp1--) {
      size_t instr_end_id = instr_idp1 - 1;
      if (!bb.instrs[instr_end_id].is(GBaseSubtype::invoke)) {
        continue;
      }
      size_t instr_start_idp1 = instr_end_id + 1;
      size_t instr_start_id = instr_end_id;
      for (instr_start_idp1 = instr_end_id; instr_start_idp1 > 0;
           instr_start_idp1--) {
        instr_start_id = instr_start_idp1 - 1;
        if (!bb.instrs[instr_start_id].is(GBaseSubtype::arg_setup)) {
          instr_start_id++;
          break;
        }
      }
      transform_call(bb.instrs, instr_start_id, instr_end_id, bb_id, lives,
                     max_types);
      // update the n of instrs since the might have changed it
      n_instrs = bb.instrs.size();
    }
    bb_id++;
  }
}

}  // namespace foptim::fmir
