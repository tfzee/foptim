#include "calling_conv.hpp"

#include <fmt/base.h>

#include <ranges>

#include "mir/analysis/cfg.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"
#include "utils/bitset.hpp"

namespace foptim::fmir {
// ###################################################################################
//  First Stage

namespace {
struct RetPosition {
  enum Type {
    IntReg,
    FloatReg,
    Stack,
  };
  Type ty;
  u32 position;
};

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
      ASSERT(conv.args.allows_stack_args);
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
      // assuming stack args are 8byte in multiple places
      ASSERT(get_size(arg_ty) <= 8);
      pos.push_back({ArgPosition::Type::Stack, stack_arg_id});
      stack_arg_id++;
    }
  }
  return stack_arg_id;
}

// u32 calculate_ret_locations(const TVec<MInstr> &args,
//                             const CallingConvDefinition &conv,
//                             TVec<RetPosition> &pos) {
//   ASSERT(conv.rets.conv == CallingConvDefinition::Ret::RetConv::NoMixing ||
//          conv.rets.conv == CallingConvDefinition::Ret::RetConv::Mixing);
//   u64 n_int_arg_regs = conv.args.gpr.size();
//   u64 n_float_arg_regs = conv.args.fvr.size();
//   u32 int_arg_id = 0;
//   u32 float_arg_id = 0;
//   u32 stack_arg_id = 0;
//   for (const auto &arg : args) {
//     const auto &arg_ty = arg.args[0].ty;
//     bool is_vec_reg = arg_ty >= Type::Float32;
//     if (is_vec_reg && float_arg_id < n_float_arg_regs) {
//       pos.push_back({RetPosition::Type::FloatReg, float_arg_id});
//       float_arg_id++;
//     } else if (!is_vec_reg && int_arg_id < n_int_arg_regs) {
//       pos.push_back({RetPosition::Type::IntReg, int_arg_id});
//       int_arg_id++;
//     } else {
//       pos.push_back({RetPosition::Type::Stack, stack_arg_id});
//       stack_arg_id++;
//     }
//   }
//   return stack_arg_id;
// }

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

void mark_returns_with_regs(IRVec<MInstr> &instrs,
                            const CallingConvDefinition &conv,
                            size_t instr_call_id, size_t instr_ret_id) {
  TVec<ArgPosition> pos;
  size_t n_rets = instr_ret_id - instr_call_id;

  ASSERT(conv.rets.conv == CallingConvDefinition::Ret::RetConv::NoMixing ||
         conv.rets.conv == CallingConvDefinition::Ret::RetConv::Mixing);
  size_t ret_gpr_indx = 0;
  size_t ret_fvr_indx = 0;

  for (u32 i = 0; i < n_rets; i++) {
    auto arg_ty = instrs[instr_call_id + i].args[0].ty;
    switch (arg_ty) {
    case Type::INVALID:
    case Type::Int8:
    case Type::Int16:
    case Type::Int32:
    case Type::Int64:
      instrs[instr_call_id + i].n_args = 2;
      instrs[instr_call_id + i].args[1] =
          MArgument({conv.rets.gpr[ret_gpr_indx], arg_ty}, arg_ty);
      ret_gpr_indx++;
      break;
    case Type::Float32:
    case Type::Float64:
    case Type::Float32x2:
    case Type::Int32x4:
    case Type::Int64x2:
    case Type::Float32x4:
    case Type::Float64x2:
    case Type::Int32x8:
    case Type::Int64x4:
    case Type::Float32x8:
    case Type::Float64x4:
    case Type::Float32x16:
    case Type::Float64x8:
      instrs[instr_call_id + i].n_args = 2;
      instrs[instr_call_id + i].args[1] =
          MArgument({conv.rets.fvr[ret_fvr_indx], arg_ty}, arg_ty);
      ret_fvr_indx++;
      break;
    }
  }
}

} // namespace

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
      for (size_t instr_call_id = instr_id; instr_call_id < n_instrs;
           instr_call_id++) {
        if (!bb.instrs[instr_call_id].is(GBaseSubtype::invoke)) {
          continue;
        }
        mark_arguments_with_regs(bb.instrs, conv, instr_id, instr_call_id);
        size_t instr_ret_id = instr_call_id + 1;
        for (; instr_ret_id < n_instrs; instr_ret_id++) {
          if (bb.instrs[instr_ret_id].is(GBaseSubtype::ret_setup)) {
            continue;
          }
          mark_returns_with_regs(bb.instrs, conv, instr_call_id + 1,
                                 instr_ret_id);
          break;
        }
        instr_id = instr_ret_id - 1;
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
  utils::BitSet<> res{static_cast<u8>(CReg::N_REGS), false};
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
          res[static_cast<u8>(arg.reg.c_reg()) - 1].set(true);
          break;
        }
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVRegScale:
        case MArgument::ArgumentType::MemVRegVRegScale: {
          res[static_cast<u8>(arg.reg.c_reg()) - 1].set(true);
          res[static_cast<u8>(arg.indx.c_reg()) - 1].set(true);
          break;
        }
        case MArgument::ArgumentType::MemImmVRegScale: {
          res[static_cast<u8>(arg.indx.c_reg()) - 1].set(true);
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
      res[static_cast<u8>(cc.args.fvr[float_arg_id]) - 1].set(true);
      float_arg_id++;
    } else if (!is_vec_reg && int_arg_id < n_int_arg_regs) {
      res[static_cast<u8>(cc.args.gpr[int_arg_id]) - 1].set(true);
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
    if (!used_regs[static_cast<u8>(reg_ty) - 1] || reg_ty == CReg::SP ||
        reg_ty == CReg::BP) {
      continue;
    }
    auto arg = MArgument{VReg{reg_ty, Type::Int64}, Type::Int64};
    first_bb.instrs.insert(first_bb.instrs.begin() + 0,
                           MInstr{GBaseSubtype::push, arg});
    n_regs_saved++;
  }

  // align the stack
  u64 additional_align_off = 0;
  if (cc.align.alignment >= CallingConvDefinition::Req::Supported) {
    auto additional_align_off = (n_regs_saved * 8) % cc.align.alignment_value;
    if (additional_align_off != 0) {
      first_bb.instrs.insert(first_bb.instrs.begin() + 0,
                             MInstr{GArithSubtype::sub2,
                                    MArgument{VReg::RSP(), Type::Int64},
                                    MArgument{additional_align_off}});
    }
  }

  // save all potential va args from registers into the register_save_area
  if (func.needs_register_save_area) {
    TODO("impl register save area");
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
      first_bb.instrs.insert(first_bb.instrs.begin() + 0,
                             MInstr{GBaseSubtype::mov,
                                    MArgument::MemOB(static_cast<i32>(offset),
                                                     VReg::RSP(), Type::Int64),
                                    MArgument{reg, reg.ty}});
    }

    // TODO: make this conitional based if there are xmm args read out of al
    // (see clang/godbolt)
    // https://godbolt.org/#g:!((g:!((g:!((h:codeEditor,i:(filename:'1',fontScale:15,fontUsePx:'0',j:1,lang:llvm,selection:(endColumn:34,endLineNumber:19,positionColumn:1,positionLineNumber:19,selectionStartColumn:34,selectionStartLineNumber:19,startColumn:1,startLineNumber:19),source:'%3B+For+Unix+x86_64+platforms,+va_list+is+the+following+struct:%0A%25struct.va_list+%3D+type+%7B+i32,+i32,+ptr,+ptr+%7D%0A%0Adefine+i32+@test(i32+%25x,+...)+%7B%0A++%3B+Initialize+variable+argument+processing%0A++%25ap+%3D+alloca+%25struct.va_list%0A++call+void+@llvm.va_start.p0(ptr+%25ap)%0A%0A++%3B+Read+a+single+integer+argument%0A++%3B%25tmp+%3D+va_arg+ptr+%25ap,+i32%0A%0A++%3B+Stop+processing+of+arguments.%0A++%3Bcall+void+@llvm.va_end.p0(ptr+%25ap)%0A++ret+i32+0%0A%7D%0A%0Adeclare+void+@llvm.va_start.p0(ptr)%0Adeclare+void+@llvm.va_copy.p0(ptr,+ptr)%0Adeclare+void+@llvm.va_end.p0(ptr)'),l:'5',n:'0',o:'LLVM+IR+source+%231',t:'0')),k:44.60580912863071,l:'4',n:'0',o:'',s:0,t:'0'),(g:!((h:compiler,i:(compiler:llctrunk,filters:(b:'0',binary:'1',binaryObject:'1',commentOnly:'0',debugCalls:'0',demangle:'0',directives:'0',execute:'1',intel:'0',libraryCode:'0',trim:'0',verboseDemangling:'0'),flagsViewOpen:'1',fontScale:18,fontUsePx:'0',j:1,lang:llvm,libs:!(),options:'',overrides:!(),selection:(endColumn:13,endLineNumber:4,positionColumn:1,positionLineNumber:3,selectionStartColumn:13,selectionStartLineNumber:4,startColumn:1,startLineNumber:3),source:1),l:'5',n:'0',o:'+llc+(trunk)+(Editor+%231)',t:'0')),k:55.39419087136929,l:'4',n:'0',o:'',s:0,t:'0')),l:'2',n:'0',o:'',t:'0')),version:4
    // TODO: should also investigate why it doesnt save xmm8-xmm15 and not
    // RDI
    for (auto [reg, offset] : arg_xmm_regs) {
      first_bb.instrs.insert(first_bb.instrs.begin() + 0,
                             MInstr{GBaseSubtype::mov,
                                    MArgument::MemOB(static_cast<i32>(offset),
                                                     VReg::RSP(), Type::Int64),
                                    MArgument{reg, reg.ty}});
    }
    first_bb.instrs.insert(first_bb.instrs.begin() + 0,
                           MInstr{GArithSubtype::sub2,
                                  MArgument{VReg::RSP(), Type::Int64},
                                  MArgument{176U}});
  }
  // after we push poped stuff to save em we then need to updated our stack
  // arguments so we actually use the right offsets.
  u32 additional_offset = 8 * (2 + n_regs_saved) + additional_align_off +
                          (func.needs_register_save_area ? 176 : 0);
  // NOTE: Assuming we got a full pro/epilogue because we reference SP

  for (auto &instr : first_bb.instrs) {
    if (instr.is(GBaseSubtype::stack_arg_load)) {
      instr.sop = static_cast<u32>(GBaseSubtype::mov);
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
      if (!used_regs[static_cast<u8>(reg_ty) - 1] || reg_ty == CReg::SP ||
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
    if (cc.align.alignment >= CallingConvDefinition::Req::Supported) {
      if (additional_align_off != 0) {
        func.bbs[bb_id].instrs.insert(
            func.bbs[bb_id].instrs.end() - 1,
            MInstr{GArithSubtype::add2, MArgument{VReg::RSP(), Type::Int64},
                   MArgument{additional_align_off}});
      }
    }
    if (func.needs_register_save_area) {
      func.bbs[bb_id].instrs.insert(func.bbs[bb_id].instrs.end() - 1,
                                    MInstr{GArithSubtype::add2,
                                           MArgument{VReg::RSP(), Type::Int64},
                                           MArgument{176U}});
    }
  }
}

static_assert(static_cast<u8>(CReg::R15) == 16);
static_assert(static_cast<u8>(CReg::A) == 1);
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

bool is_alive(VReg reg_ty, TMap<VReg, LinearRangeSet> &lives, size_t start,
              size_t end, size_t bb_id) {
  if (!lives.contains(reg_ty)) {
    return false;
  }
  return lives.at(reg_ty).collide(LinearRange::inBB(bb_id, start, end + 1));
}

Type get_save_type(CReg reg_ty, const TMap<CReg, Type> &max_types) {
  if (reg_ty >= CReg::mm0) {
    auto it = max_types.find(reg_ty);
    Type max_ty = it != max_types.end() ? it->second : Type::Float64;
    if (max_ty == Type::Float32) {
      return Type::Float32;
    }
    if (max_ty == Type::Float64) {
      return Type::Float64;
    }
    return Type::Int64x4;
  }
  return Type::Int64;
}

struct CallInfo {
  size_t start_id;
  size_t call_id;
  size_t end_id;
  size_t bb_id;
  TVec<MInstr> &args;
  TVec<MInstr> &rets;
  TMap<VReg, LinearRangeSet> &lives;
  utils::BitSet<> &return_values_overwrites_ret_reg;
  const CallingConvDefinition &cc;
};

void save_locals(IRVec<MInstr> &instrs, CallInfo &cinfo, MInstr &call,
                 const TMap<CReg, Type> &max_types) {
  (void)call;
  for (auto reg_ty : cinfo.cc.caller_saved | std::views::reverse) {
    if (!cinfo.rets.empty()) {
      if (cinfo.return_values_overwrites_ret_reg[static_cast<u64>(reg_ty)]) {
        continue;
      }
    }
    if (!is_alive(VReg{reg_ty}, cinfo.lives, cinfo.call_id, cinfo.call_id + 1,
                  cinfo.bb_id) ||
        reg_ty == CReg::SP || reg_ty == CReg::BP) {
      continue;
    }
    // fmt::println("SAVE LOCAL REG {}:", VReg{reg_ty});
    // cinfo.lives.at(VReg{reg_ty}).dump();

    // fmt::println("> {}", VReg{reg_ty});
    // cinfo.lives[reg_ty].dump();
    // fmt::println("=============");
    // LinearRange::inBB(cinfo.bb_id, cinfo.call_id - 1,
    // cinfo.call_id).dump();
    // fmt::println("\n=============");

    auto push_ty = get_save_type(reg_ty, max_types);
    auto arg = MArgument{VReg{reg_ty, push_ty}, push_ty};
    instrs.insert(instrs.begin() + static_cast<i64>(cinfo.start_id),
                  MInstr{GBaseSubtype::push, arg});
  }
  // for (const auto &ret : cinfo.rets) {
  //   auto reg1_ty = ret.args[1].reg.c_reg();
  //   if (cinfo.return_values_overwrites_ret_reg[(u64)reg1_ty]) {
  //     continue;
  //   }
  //   if (is_alive(VReg{reg1_ty}, cinfo.lives, cinfo.call_id, cinfo.call_id +
  //   1,
  //                cinfo.bb_id)) {
  //     auto save_ty = get_save_type(reg1_ty, max_types);
  //     auto arg = MArgument{VReg{reg1_ty, save_ty}, save_ty};
  //     instrs.insert(instrs.begin() + (i64)cinfo.start_id,
  //                   MInstr{GBaseSubtype::push, arg});
  //   }
  // }
}

std::pair<uint32_t, uint32_t>
restore_locals(IRVec<MInstr> &instrs, CallInfo &cinfo,
               const TMap<CReg, Type> &max_types) {
  uint32_t n_locals_restored = 0;
  uint32_t n_local_bytes_restored = 0;

  // TVec<CReg> skip_regs{};
  // ret value
  // for (auto &ret_reg : cinfo.rets) {
  //   auto ret_reg_type = ret_reg.args[1].reg.c_reg();
  //   if (cinfo.return_values_overwrites_ret_reg[(u64)ret_reg_type]) {
  //     skip_regs.push_back(ret_reg_type);
  //     continue;
  //   }
  //   // actually in use
  //   bool is_reg_alive = is_alive(VReg{CReg::mm1}, cinfo.lives, cinfo.call_id,
  //                                cinfo.call_id + 1, cinfo.bb_id);
  //   if (!is_reg_alive) {
  //     skip_regs.push_back(ret_reg_type);
  //     continue;
  //   }
  //   auto save_ty = get_save_type(ret_reg_type, max_types);
  //   auto arg = MArgument{VReg{ret_reg_type, save_ty}, save_ty};
  //   instrs.insert(instrs.begin() + (i64)cinfo.start_id,
  //                 MInstr{GBaseSubtype::pop, arg});
  //   n_locals_restored++;
  //   n_local_bytes_restored += get_size(save_ty);
  // }

  // fmt::println("> Got {} skip regs", skip_regs.size());
  for (auto reg_ty : cinfo.cc.caller_saved) {
    if (!cinfo.rets.empty()) {
      if (cinfo.return_values_overwrites_ret_reg[static_cast<u64>(reg_ty)]) {
        // skip_regs.push_back(ret_reg_type);
        continue;
      }
    }
    // if (std::find(skip_regs.begin(), skip_regs.end(), reg_ty) !=
    //     skip_regs.end()) {
    //   continue;
    // }
    if (reg_ty == CReg::SP || reg_ty == CReg::BP ||
        !is_alive(VReg{reg_ty}, cinfo.lives, cinfo.call_id, cinfo.call_id + 1,
                  cinfo.bb_id)) {
      continue;
    }
    auto push_ty = get_save_type(reg_ty, max_types);
    n_locals_restored += 1;
    n_local_bytes_restored += get_size(push_ty);
    auto arg = MArgument{VReg{reg_ty, push_ty}, push_ty};
    instrs.insert(instrs.begin() + static_cast<i64>(cinfo.start_id),
                  MInstr{GBaseSubtype::pop, arg});
  }
  return {n_locals_restored, n_local_bytes_restored};
}

void generate_arg(TVec<MInstr> &instrs, const MInstr &arg,
                  const CallingConvDefinition &cconf,
                  const ArgPosition &arg_pos) {
  const auto &arg_ty = arg.args[0].ty;
  switch (arg_pos.ty) {
  case ArgPosition::IntReg:
    instrs.emplace_back(
        GBaseSubtype::mov,
        MArgument{{cconf.args.gpr[arg_pos.position], arg_ty}, arg_ty},
        arg.args[0]);
    break;
  case ArgPosition::FloatReg:
    instrs.emplace_back(
        GBaseSubtype::mov,
        MArgument{{cconf.args.fvr[arg_pos.position], arg_ty}, arg_ty},
        arg.args[0]);
    break;
  case ArgPosition::Stack:
    instrs.emplace_back(GBaseSubtype::push, arg.args[0]);
    break;
  }
}

void setup_call_returns(IRVec<MInstr> &out_instrs, CallInfo &cinfo) {
  for (auto ret : cinfo.rets | std::ranges::views::reverse) {
    const auto &ret_ty = ret.args[0].ty;
    switch (ret_ty) {
    case Type::INVALID:
      UNREACH();
    case Type::Int8:
    case Type::Int16:
    case Type::Int32:
    case Type::Int64:
      out_instrs.insert(out_instrs.begin() + cinfo.start_id,
                        {
                            GBaseSubtype::mov,
                            ret.args[0],
                            ret.args[1],
                        });
      break;
    case Type::Float32:
    case Type::Float64:
    case Type::Float32x2:
    case Type::Int32x4:
    case Type::Int64x2:
    case Type::Float32x4:
    case Type::Float64x2:
    case Type::Int32x8:
    case Type::Int64x4:
    case Type::Float32x8:
    case Type::Float64x4:
    case Type::Float32x16:
    case Type::Float64x8:
      out_instrs.insert(out_instrs.begin() + cinfo.start_id,
                        {
                            GBaseSubtype::mov,
                            ret.args[0],
                            ret.args[1],
                        });
      break;
    }
  }
}
void setup_call_arguments(IRVec<MInstr> &out_instrs,
                          const TVec<ArgPosition> &arg_pos, CallInfo &cinfo) {
  TVec<MInstr> output_vec;
  TVec<u32> worklist;
  for (size_t arg_id = 0; arg_id < cinfo.args.size(); arg_id++) {
    if (arg_pos[arg_id].ty == ArgPosition::Type::Stack) {
      generate_arg(output_vec, cinfo.args[arg_id], cinfo.cc, arg_pos[arg_id]);
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
        wants_reg = cinfo.cc.args.gpr[arg_pos[arg_id].position];
        break;
      case ArgPosition::FloatReg:
        wants_reg = cinfo.cc.args.fvr[arg_pos[arg_id].position];
        break;
      case ArgPosition::Stack:
        UNREACH();
        break;
      }

      bool collision = false;
      for (size_t other_id = 0; other_id < worklist.size(); other_id++) {
        if (other_id != curr_work_item &&
            cinfo.args[worklist[other_id]].args[0].uses_same_vreg(wants_reg)) {
          collision = true;
          break;
        }
      }
      if (!collision) {
        generate_arg(output_vec, cinfo.args[arg_id], cinfo.cc, arg_pos[arg_id]);
        worklist.erase(worklist.begin() + curr_work_item);
        found_one = true;
        break;
      }
    }
    // if we didnt find any that dont colllide
    // we us a push and pop
    if (!found_one && !worklist.empty()) {
      output_vec.emplace_back(GBaseSubtype::push,
                              cinfo.args[worklist[0]].args[0]);
      push_pop_queue.push_back(worklist[0]);
      worklist.erase(worklist.begin() + 0);
    }
  }

  for (auto push_pop : push_pop_queue | std::views::reverse) {
    const auto &arg = cinfo.args[push_pop];
    const auto &arg_ty = arg.args[0].ty;
    auto arg_po = arg_pos[push_pop];
    switch (arg_po.ty) {
    case ArgPosition::IntReg:
      output_vec.emplace_back(
          GBaseSubtype::pop,
          MArgument{{cinfo.cc.args.gpr[arg_po.position], arg_ty}, arg_ty});
      break;
    case ArgPosition::FloatReg:
      output_vec.emplace_back(
          GBaseSubtype::pop,
          MArgument{{cinfo.cc.args.fvr[arg_po.position], arg_ty}, arg_ty});
      break;
    case ArgPosition::Stack:
      UNREACH();
    }
  }
  out_instrs.insert(out_instrs.begin() + cinfo.start_id, output_vec.begin(),
                    output_vec.end());
}

void transform_call(IRVec<MInstr> &instrs, const CallingConvDefinition &cc,
                    size_t start_id, size_t call_id, size_t end_id,
                    size_t bb_id, TMap<VReg, LinearRangeSet> &lives,
                    const TMap<CReg, Type> &max_types) {
  size_t n_args = call_id - start_id;
  size_t n_rets = end_id - (call_id + 1);

  TVec<MInstr> args;
  TVec<MInstr> rets;
  MInstr call = instrs[call_id];
  auto is_var_args = call.is_var_arg_call;
  args.reserve(n_args);

  for (u32 i = 0; i < n_args; i++) {
    args.push_back(instrs.at(start_id + i));
  }
  for (u32 i = 0; i < n_rets; i++) {
    rets.push_back(instrs.at(call_id + 1 + i));
  }

  // fmt::println("CALL:  RETS:{} <- ARGS:{}", n_rets, n_args);
  // for (auto arg : args) {
  //   fmt::println(" A: {}", arg);
  // }
  // for (auto ret : rets) {
  //   fmt::println(" R: {}", ret);
  // }

  instrs.erase(instrs.begin() + static_cast<i64>(start_id),
               instrs.begin() + static_cast<i64>(end_id));

  utils::BitSet<> return_values_overwrites_ret_reg =
      utils::BitSet<>::empty(static_cast<size_t>(CReg::N_REGS));

  CallInfo cinfo = {
      .start_id = start_id,
      .call_id = call_id,
      .end_id = end_id,
      .bb_id = bb_id,
      .args = args,
      .rets = rets,
      .lives = lives,
      .return_values_overwrites_ret_reg = return_values_overwrites_ret_reg,
      .cc = cc,
  };

  if (!rets.empty()) {
    for (auto &ret : rets) {
      // TODO: could do more fancy check checking if other ret override this
      //  we override the result so dont care
      if (ret.args[0] != ret.args[1]) {
        continue;
      }
      ASSERT(ret.args[1].isReg() && ret.args[1].reg.is_concrete());
      return_values_overwrites_ret_reg[static_cast<u64>(
                                           ret.args[1].reg.c_reg())]
          .set(true);
    }
  }

  const auto [n_locals_saved, n_local_bytes_need_saving] =
      restore_locals(instrs, cinfo, max_types);

  TVec<ArgPosition> arg_pos;
  auto n_stack_args = calculate_arg_locations(args, cc, arg_pos);
  if (cc.align.alignment >= CallingConvDefinition::Req::Supported) {
    auto off = (n_local_bytes_need_saving + n_stack_args * 8) %
               cc.align.alignment_value;
    if (off != 0) {
      instrs.insert(instrs.begin() + static_cast<i64>(cinfo.start_id) +
                        static_cast<i64>(n_locals_saved),
                    MInstr{GArithSubtype::add2,
                           MArgument{VReg::RSP(), Type::Int64},
                           MArgument{off}});
    }
  }

  // cleanup args
  {
    ASSERT(cc.args.caller_cleanup);
    auto sp = MArgument{VReg::RSP(), Type::Int64};
    if (n_stack_args > 0) {
      instrs.insert(instrs.begin() + static_cast<i64>(cinfo.start_id),
                    MInstr{GArithSubtype::add2, sp, 8 * n_stack_args});
    }
  }
  // setup returns
  setup_call_returns(instrs, cinfo);
  // do call
  ASSERT(!cinfo.cc.link_reg.has_value());
  instrs.insert(instrs.begin() + static_cast<i64>(cinfo.start_id),
                MInstr{GBaseSubtype::call, call.args[0]});
  instrs[cinfo.start_id].is_var_arg_call = is_var_args;

  // for vararg setup al with the number of xmm registiers
  if (is_var_args) {
    u8 n_gpr_regs = 0;
    u8 n_fvr_regs = 0;
    for (auto arg_p : arg_pos) {
      if (arg_p.ty == ArgPosition::FloatReg) {
        n_fvr_regs++;
      }
      if (arg_p.ty == ArgPosition::IntReg) {
        n_gpr_regs++;
      }
    }
    if (cinfo.cc.var_arg.n_gpr_regs_stor.has_value()) {
      instrs.insert(
          instrs.begin() + static_cast<i64>(cinfo.start_id),
          MInstr{GBaseSubtype::mov,
                 MArgument{
                     VReg{cinfo.cc.var_arg.n_gpr_regs_stor.value(), Type::Int8},
                     Type::Int8},
                 MArgument{(n_gpr_regs)}});
    }
    if (cinfo.cc.var_arg.n_fvr_regs_stor.has_value()) {
      instrs.insert(
          instrs.begin() + static_cast<i64>(cinfo.start_id),
          MInstr{GBaseSubtype::mov,
                 MArgument{
                     VReg{cinfo.cc.var_arg.n_fvr_regs_stor.value(), Type::Int8},
                     Type::Int8},
                 MArgument{(n_fvr_regs)}});
    }
  }

  // setup args
  setup_call_arguments(instrs, arg_pos, cinfo);
  // save locals
  save_locals(instrs, cinfo, call, max_types);

  auto off =
      (n_local_bytes_need_saving + n_stack_args * 8) % cc.align.alignment_value;
  if (off != 0) {
    instrs.insert(instrs.begin() + static_cast<i64>(cinfo.start_id),
                  MInstr{GArithSubtype::sub2,
                         MArgument{VReg::RSP(), Type::Int64}, MArgument{off}});
  }
}

} // namespace

void CallingConvImpl::second_stage(MFunc &func, const CallingConvDefinition &cc,
                                   const conf::CompConf &) {
  (void)func;
  (void)cc;
  ASSERT(!func.variadic ||
         cc.var_arg.supported >= CallingConvDefinition::Req::Supported);
  ASSERT(cc.var_arg.supported != CallingConvDefinition::Req::Required ||
         func.variadic);
  CFG cfg(func);
  save_regs_callee(func, cc, cfg);
  // fmt::println("===============RUNING CC=====================");
  // fmt::println("{}", func);
  TMap<VReg, LinearRangeSet> lives = linear_lifetime(func);
  TMap<CReg, Type> max_types = compute_max_reg_types(func);

  size_t bb_id = 0;
  for (auto &bb : func.bbs) {
    size_t n_instrs = bb.instrs.size();
    for (size_t instr_idp1 = n_instrs; instr_idp1 > 0; instr_idp1--) {
      size_t instr_invoke_id = instr_idp1 - 1;
      if (!bb.instrs[instr_invoke_id].is(GBaseSubtype::invoke)) {
        continue;
      }
      size_t instr_start_idp1;
      size_t instr_start_id = instr_invoke_id;
      for (instr_start_idp1 = instr_invoke_id; instr_start_idp1 > 0;
           instr_start_idp1--) {
        instr_start_id = instr_start_idp1 - 1;
        if (!bb.instrs[instr_start_id].is(GBaseSubtype::arg_setup)) {
          instr_start_id++;
          break;
        }
      }
      size_t instr_end_id = instr_invoke_id + 1;
      for (; instr_end_id < n_instrs; instr_end_id++) {
        if (!bb.instrs[instr_end_id].is(GBaseSubtype::ret_setup)) {
          break;
        }
      }
      // fmt::println("CALL TRANSFORM:");
      // for (auto i = instr_start_id; i < instr_end_id; i++) {
      //   fmt::println(" >> {}", bb.instrs[i]);
      // }
      transform_call(bb.instrs, cc, instr_start_id, instr_invoke_id,
                     instr_end_id, bb_id, lives, max_types);
      // update the n of instrs since the might have changed it
      n_instrs = bb.instrs.size();
    }
    bb_id++;
  }
}

} // namespace foptim::fmir
