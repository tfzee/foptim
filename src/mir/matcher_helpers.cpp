#include "mir/matcher_helpers.hpp"

#include <typeinfo>

#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "mir/matcher.hpp"
#include "utils/set.hpp"

namespace foptim::fmir {

MArgument get_or_insert_bbarg_mapping(fir::BBArgument arg, MatchResult &res,
                                      ExtraMatchData &data) {
  if (!data.bb_arg_mapping.contains(arg)) {
    // we only need this extra register mapping
    //  iff we use the bbarg either in another bb_arg the original bbs
    //  terminator or another bb that isnt the bb itself
    //
    // for now we can just check if its only used in the definition bb
    // and not both terminators targets(if conditional) end up in teh
    // definitions bb
    bool safe_to_not_copy = true;

    auto def_bb = arg->get_parent();
    {
      auto def_term = def_bb->get_terminator();
      u32 n_hits = 0;
      if (def_term->bbs.size() > 1) {
        for (auto &bb : def_term->bbs) {
          if (bb.bb == def_bb) {
            n_hits++;
          }
        }
      }
      safe_to_not_copy = n_hits <= 1;
    }
    if (safe_to_not_copy) {
      // just checking the uses is not enough as it turns out
      //  we could have a bbarg which is used by and add instruction inside the
      //  bb making it look safe to omit the copy. But if that add is used by
      //  for example a load later on this add might be merged into the future
      //  load which then makes it unsafe to omit the copy.
      // technically there are only a few instructions which could lead to such
      //  a forward merge but since this might change in the future and its hard
      //  to detect this i will just emit a copy everytime theres a use by a use
      //  by a use... that is used outside of this bb
      TSet<fir::Instr> checked;
      TVec<fir::Instr> worklist;
      for (auto use : arg->uses) {
        if (use.user->parent != def_bb) {
          safe_to_not_copy = false;
          break;
        }
        checked.insert(use.user);
        for (auto u : use.user->uses) {
          worklist.push_back(u.user);
        }
      }
      while (!worklist.empty()) {
        auto u = worklist.back();
        checked.insert(u);
        worklist.pop_back();
        if (u->parent != def_bb) {
          safe_to_not_copy = false;
          break;
        }
        for (auto use : u->uses) {
          if (!checked.contains(use.user)) {
            worklist.push_back(use.user);
          }
        }
      }
    }
    if (safe_to_not_copy) {
      data.bb_arg_mapping.insert(
          {arg, valueToArg(fir::ValueR{arg}, res.result, data.alloc)});
    } else {
      auto arg_ty = convert_type(arg->get_type());
      auto to = data.alloc.get_new_register(arg_ty);
      data.bb_arg_mapping.insert({arg, MArgument{to, arg_ty}});
    }
  }
  return data.bb_arg_mapping.at(arg);
}

namespace {
MArgument setup_callarg(fir::ValueR arg, MatchResult &res,
                        ExtraMatchData &data) {
  if (!arg.is_instr()) {
    return valueToArg(arg, res.result, data.alloc);
  }
  return valueToArg(arg, res.result, data.alloc);
}
}  // namespace

void setup_callargs(fir::Instr &call_instr, MatchResult &res,
                    ExtraMatchData &data) {
  // fmt::println("Instr: {}", call_instr);
  TVec<MArgument> evaluated_args;
  for (size_t arg_id = 1; arg_id < call_instr->args.size(); arg_id++) {
    evaluated_args.push_back(
        setup_callarg(call_instr->args[arg_id], res, data));
  }
  for (auto arg_value : evaluated_args) {
    res.result.emplace_back(GBaseSubtype::arg_setup, arg_value);
  }
}

MArgument valueToArgConst(fir::ValueR val, TVec<MInstr> &res,
                          DumbRegAlloc &alloc) {
  ASSERT(val.is_constant());
  auto consti = val.as_constant();
  if (consti->is_int()) {
    if (val.get_type()->is_ptr()) {
      return {(u64)std::bit_cast<u64>((i64)consti->as_int())};
    }
    switch (val.get_type()->as_int()) {
      case 1:
      case 8:
        return MArgument::Int(std::bit_cast<u64>((i64)(i8)consti->as_int()),
                              Type::Int8);
      case 16:
        return MArgument::Int(std::bit_cast<u64>((i64)(i16)consti->as_int()),
                              Type::Int16);
        return {};
      case 32:
        return MArgument::Int(std::bit_cast<u64>((i64)(i32)consti->as_int()),
                              Type::Int32);
      case 64:
        return MArgument::Int(std::bit_cast<u64>((i64)consti->as_int()),
                              Type::Int64);
      default:
        fmt::println("{}", (i64)consti->as_int());
        TODO("impl");
    }
  }
  if (consti->is_null()) {
    return {(u64)0};
  }

  if (consti->is_float()) {
    if (val.get_type()->as_float() == 32) {
      return {consti->as_f32()};
    }
    return {consti->as_f64()};
  }

  if (consti->is_global()) {
    auto global = consti->as_global();
    if (global->init_value == nullptr) {
      if (global->is_extern_decl()) {
        Type type_id = convert_type(val.get_type());
        auto helper = alloc.get_new_register(Type::Int64);
        res.emplace_back(GBaseSubtype::mov, MArgument(helper, Type::Int64),
                         MArgument::MemL(global->name.c_str(), type_id));
        return MArgument(helper, Type::Int64);
      }
    }
    // TODO: idk if i64 is right here
    Type type_id = convert_type(val.get_type());
    auto arg = MArgument::MemL(global->name.c_str(), type_id);
    auto helper = MArgument{alloc.get_new_register(Type::Int64), Type::Int64};
    res.emplace_back(X86Subtype::lea, helper, arg);
    return helper;
  }
  if (consti->is_func()) {
    auto funcy = consti->as_func();
    auto arg = MArgument::MemL(funcy->getName().c_str(), Type::Int64);
    auto helper = MArgument(alloc.get_new_register(Type::Int64), Type::Int64);
    res.emplace_back(X86Subtype::lea, helper, arg);
    return helper;
  }
  if (consti->is_poison()) {
    switch (consti->type->ty) {
      case fir::AnyTypeType::Integer:
        switch (consti->type->as_int()) {
          case 1:
            return {(u8)0};
          case 8:
            return {(u8)0};
          case 16:
            return {(u16)0};
          case 32:
            return {(u32)0};
          case 64:
            return {(u64)0};
          default:
            fmt::println("{}", consti->type->as_int());
            UNREACH();
        }
      case fir::AnyTypeType::Ptr:
        return {(u64)0};
      case fir::AnyTypeType::Float:
        return {0.0F};
      case fir::AnyTypeType::Vector: {
        Type type_id = convert_type(val.get_type());
        auto arg = MArgument{VReg{CReg::mm0, type_id}, type_id};
        res.emplace_back(GVecSubtype::fxor, arg, arg, arg);
        return arg;
      }
      case fir::AnyTypeType::Function:
      case fir::AnyTypeType::Void:
      case fir::AnyTypeType::Struct:
        fmt::println("{} with type {}", consti, consti->type);
        UNREACH();
        break;
    }
  }
  fmt::println("{}", consti);
  UNREACH();
}

MArgument valueToArg(fir::ValueR val, TVec<MInstr> &res, DumbRegAlloc &alloc) {
  if (val.is_constant()) {
    return valueToArgConst(val, res, alloc);
  }
  Type type_id = convert_type(val.get_type());
  return {alloc.get_register(val), type_id};
}

MArgument valueToArgPosMem(fir::ValueR val, TVec<MInstr> &res,
                           DumbRegAlloc &alloc, fir::BasicBlock curr_bb) {
  if (val.is_constant()) {
    return valueToArgConst(val, res, alloc);
  }
  if (val.is_instr()) {
    auto load = val.as_instr();
    // TODO: verify if this is ok for atomic/volatile
    if (load->is(fir::InstrType::LoadInstr) &&
        !load->args[0].get_type()->is_vec() && !load->Atomic) {
      if (curr_bb == load->get_parent()) {
        auto ptr = valueToArgPtrSmart(
            load->args[0], convert_type(load.get_type()), res, alloc);
        return ptr;
      }
    }
  }
  Type type_id = convert_type(val.get_type());
  return {alloc.get_register(val), type_id};
}

TVec<MArgument> valueToArgStruct(fir::ValueR val, TVec<MInstr> &res,
                                 DumbRegAlloc &alloc) {
  if (!val.get_type()->is_struct()) {
    return {valueToArg(val, res, alloc)};
  }
  auto t = val.get_type()->as_struct();
  if (val.is_constant() && !val.as_constant()->is_poison()) {
    TODO("impl struct constant");
  }
  TVec<MArgument> result;
  result.reserve(t.elems.size());

  u32 id = 0;
  for (auto m : t.elems) {
    Type type_id = convert_type(m.ty);
    auto new_reg = alloc.get_struct_register(val, m.ty, id);
    result.emplace_back(new_reg, type_id);
    // res.emplace_back(GBaseSubtype::mov, MArgument{new_reg, type_id},
    //                  MArgument::Int(0, type_id));
    id++;
  }
  return result;
}

MArgument valueToArgPtr(fir::ValueR val, Type type_id, TVec<MInstr> &res,
                        DumbRegAlloc &alloc) {
  if (val.is_constant()) {
    auto constant = val.as_constant();
    if (constant->is_global()) {
      auto global = constant->as_global();
      if (global->is_extern_decl()) {
        Type type_id = convert_type(val.get_type());
        auto helper = alloc.get_new_register(Type::Int64);
        res.emplace_back(GBaseSubtype::mov, MArgument(helper, Type::Int64),
                         MArgument::MemL(global->name.c_str(), type_id));
        return MArgument::MemB(helper, type_id);
      }
      Type type_id = convert_type(val.get_type());
      // TODO: idk if i64 is right here
      return MArgument::MemL(global->name.c_str(), type_id);
    }
    if (constant->is_func()) {
      auto funcy = constant->as_func();
      return {funcy->getName().c_str()};
    }
    if (constant->is_int()) {
      auto constant_ptr = constant->as_int();
      return MArgument::MemO((u64)constant_ptr, type_id);
    }
    if (constant->is_null() || constant->is_poison()) {
      return MArgument::MemO((u64)0, type_id);
    }
    fmt::println("{}", constant);
    TODO("unreach?");
    // return {(u64)0, :wype_id};
  } else {
    return MArgument::MemB(alloc.get_register(val), type_id);
  }
  fmt::println("{}", val);
  ASSERT(false);
  std::abort();
}

MArgument valueToArgPtrSmart(fir::ValueR val, Type type_id, TVec<MInstr> &res,
                             DumbRegAlloc &alloc) {
  if (val.is_constant()) {
    auto constant = val.as_constant();
    if (constant->is_global()) {
      auto global = constant->as_global();
      if (global->init_value == nullptr) {
        if (global->is_extern_decl()) {
          Type type_id = convert_type(val.get_type());
          auto helper = alloc.get_new_register(Type::Int64);
          res.emplace_back(GBaseSubtype::mov, MArgument(helper, Type::Int64),
                           MArgument::MemL(global->name.c_str(), type_id));
          return MArgument::MemB(helper, type_id);
        }
      }
      // Type type_id = convert_type(val.get_type());
      return MArgument::MemL(global->name.c_str(), type_id);
    }
    if (constant->is_func()) {
      auto funcy = constant->as_func();
      return {funcy->getName().c_str()};
    }
    if (constant->is_int()) {
      auto constant_ptr = constant->as_int();
      return MArgument::MemO((u64)constant_ptr, type_id);
    }
    if (constant->is_null() || constant->is_poison()) {
      return MArgument::MemO((u64)0, type_id);
    }
    fmt::println("{}", constant);
    TODO("unreach?");
    // return {(u64)0, :wype_id};
  } else if (val.is_instr()) {
    auto i = val.as_instr();
    if (i->is(fir::BinaryInstrSubType::IntAdd)) {
      if (i->args[0].is_instr() &&
          i->args[0].as_instr()->is(fir::BinaryInstrSubType::IntAdd)) {
        auto i2 = i->args[0].as_instr();
        auto arg1 = valueToArg(i2->args[0], res, alloc);
        auto arg2 = valueToArg(i2->args[1], res, alloc);
        auto arg3 = valueToArg(i->args[1], res, alloc);
        // does order matter here ?
        if (arg1.isReg() && arg2.isReg() && arg3.isImm()) {
          return MArgument::MemOBI(arg3.imm, arg1.reg, arg2.reg, type_id);
        } else if (arg1.isReg() && arg3.isReg() && arg2.isImm()) {
          return MArgument::MemOBI(arg2.imm, arg1.reg, arg3.reg, type_id);
        } else if (arg2.isReg() && arg3.isReg() && arg1.isImm()) {
          return MArgument::MemOBI(arg1.imm, arg2.reg, arg3.reg, type_id);
        }
      }
      if (i->args[1].is_instr() &&
          i->args[1].as_instr()->is(fir::BinaryInstrSubType::IntAdd)) {
        auto i2 = i->args[1].as_instr();
        auto arg1 = valueToArg(i2->args[0], res, alloc);
        auto arg2 = valueToArg(i2->args[1], res, alloc);
        auto arg3 = valueToArg(i->args[0], res, alloc);
        // does order matter here ?
        if (arg1.isReg() && arg2.isReg() && arg3.isImm()) {
          return MArgument::MemOBI(arg3.imm, arg1.reg, arg2.reg, type_id);
        } else if (arg1.isReg() && arg3.isReg() && arg2.isImm()) {
          return MArgument::MemOBI(arg2.imm, arg1.reg, arg3.reg, type_id);
        } else if (arg2.isReg() && arg3.isReg() && arg1.isImm()) {
          return MArgument::MemOBI(arg1.imm, arg2.reg, arg3.reg, type_id);
        }
      }
      if (i->args[0].is_instr() &&
          i->args[0].as_instr()->is(fir::BinaryInstrSubType::IntMul)) {
        auto i2 = i->args[0].as_instr();
        if (i2->args[1].is_const_int(1) || i2->args[1].is_const_int(2) ||
            i2->args[1].is_const_int(4) || i2->args[1].is_const_int(8)) {
          auto arg1 = valueToArg(i2->args[0], res, alloc);
          auto scale = valueToArg(i2->args[1], res, alloc);
          auto off = valueToArg(i->args[1], res, alloc);

          ASSERT(scale.isImm());

          u8 log_scale = 0;
          switch (scale.imm) {
            default:
              UNREACH();
            case 1:
              log_scale = 0;
              break;
            case 2:
              log_scale = 1;
              break;
            case 4:
              log_scale = 2;
              break;
            case 8:
              log_scale = 3;
              break;
          }
          // does order matter here ?
          if (arg1.isReg() && off.isReg()) {
            return MArgument::MemBIS(off.reg, arg1.reg, log_scale, type_id);
          } else if (arg1.isReg() && off.isImm()) {
            return MArgument::MemOIS(off.imm, arg1.reg, log_scale, type_id);
          }
        }
      }
      if (i->args[1].is_instr() &&
          i->args[1].as_instr()->is(fir::BinaryInstrSubType::IntMul)) {
        auto i2 = i->args[1].as_instr();
        if (i2->args[1].is_const_int(1) || i2->args[1].is_const_int(2) ||
            i2->args[1].is_const_int(4) || i2->args[1].is_const_int(8)) {
          auto arg1 = valueToArg(i2->args[0], res, alloc);
          auto scale = valueToArg(i2->args[1], res, alloc);
          auto off = valueToArg(i->args[0], res, alloc);
          ASSERT(scale.isImm());
          u8 log_scale = 0;
          switch (scale.imm) {
            default:
              UNREACH();
            case 1:
              log_scale = 0;
              break;
            case 2:
              log_scale = 1;
              break;
            case 4:
              log_scale = 2;
              break;
            case 8:
              log_scale = 3;
              break;
          }
          // does order matter here ?
          if (arg1.isReg() && off.isReg()) {
            return MArgument::MemBIS(off.reg, arg1.reg, log_scale, type_id);
          } else if (arg1.isReg() && off.isImm()) {
            return MArgument::MemOIS(off.imm, arg1.reg, log_scale, type_id);
          }
        }
      }
      auto arg1 = valueToArg(i->args[0], res, alloc);
      auto arg2 = valueToArg(i->args[1], res, alloc);
      if (arg1.isLabel() && arg2.isImm()) {
        return MArgument::MemLO(arg1.label, arg2.imm, type_id);
      }
      if (arg1.isReg() && arg2.isReg()) {
        return MArgument::MemBI(arg1.reg, arg2.reg, type_id);
      }
      if (arg1.isReg() && arg2.isReg()) {
        return MArgument::MemBI(arg1.reg, arg2.reg, type_id);
      }
    }
    return MArgument::MemB(alloc.get_register(val), type_id);

  } else {
    return MArgument::MemB(alloc.get_register(val), type_id);
  }
  fmt::println("{}", val);
  ASSERT(false);
  std::abort();
}

void setup_va_start(fir::Instr &va_instr, MatchResult &res,
                    ExtraMatchData &data) {
  auto ptr_arg = valueToArg(va_instr->args[0], res.result, data.alloc);
  // TODO: handle other cases aswell
  ASSERT(ptr_arg.isReg());

  auto n_int_args = 0;
  auto n_vec_args = 0;
  // TODO: idk about this
  for (auto arg : data.func.args) {
    if (arg.is_vec_reg()) {
      n_vec_args += 1;
    } else {
      n_int_args += 1;
    }
  }

  res.result.emplace_back(GBaseSubtype::mov,
                          MArgument::MemOB(0, ptr_arg.reg, Type::Int32),
                          MArgument((u32)8 * n_int_args));
  res.result.emplace_back(GBaseSubtype::mov,
                          MArgument::MemOB(4, ptr_arg.reg, Type::Int32),
                          MArgument((u32)48 + 16 * n_vec_args));
  res.result.emplace_back(GBaseSubtype::mov,
                          MArgument::MemOB(8, ptr_arg.reg, Type::Int64),
                          MArgument(VReg::RBP(), Type::Int64));
  res.result.emplace_back(GArithSubtype::add2,
                          MArgument::MemOB(8, ptr_arg.reg, Type::Int64),
                          MArgument((u16)16));
  res.result.emplace_back(GBaseSubtype::mov,
                          MArgument::MemOB(16, ptr_arg.reg, Type::Int64),
                          MArgument(VReg::RBP(), Type::Int64));
  res.result.emplace_back(GArithSubtype::sub2,
                          MArgument::MemOB(16, ptr_arg.reg, Type::Int64),
                          MArgument((u16)176));
}

void setup_va_end(fir::Instr &va_instr, MatchResult &res,
                  ExtraMatchData &data) {
  (void)va_instr;
  (void)res;
  (void)data;
}

bool generate_lea_from_cmult(MArgument res_reg, VReg helper_reg, VReg arg0,
                             i128 consti_val, TVec<MInstr> &result,
                             Type res_ty) {
  // x*c where c is either 2 4 8  or 3 5 9
  auto res_ty_size = get_size(res_ty);
  if (res_ty_size != 8 && res_ty_size != 4 && res_ty_size != 2) {
    return false;
  }
  bool mul1More = false;
  bool mul1Less = false;
  auto base = MArgument(arg0, res_ty);

  switch (consti_val) {
    default: {
      // fmt::println("Failed simplify mul x*{}", consti_val);
      // TODO("failed");
      return false;
    }
    case 1:
      consti_val = 0;
      break;
    case 2:
      consti_val = 1;
      break;
    case 3:
      consti_val = 1;
      mul1More = true;
      break;
    case 4:
      consti_val = 2;
      break;
    case 5:
      consti_val = 2;
      mul1More = true;
      break;
    case 6: {
      auto helper_arg = MArgument(helper_reg, res_ty);
      result.emplace_back(GBaseSubtype::mov, helper_arg, base);
      result.emplace_back(GArithSubtype::add2, helper_arg, helper_arg);
      result.emplace_back(X86Subtype::lea, res_reg,
                          MArgument::MemBIS(helper_reg, helper_reg, 2, res_ty));
      return true;
    }
    case 7:
      consti_val = 3;
      mul1Less = true;
      break;
    case 8:
      consti_val = 3;
      break;
    case 9:
      consti_val = 3;
      mul1More = true;
      break;
    case 10: {
      auto helper_arg = MArgument(helper_reg, res_ty);
      result.emplace_back(GBaseSubtype::mov, helper_arg, base);
      result.emplace_back(GArithSubtype::add2, helper_arg, helper_arg);
      result.emplace_back(X86Subtype::lea, res_reg,
                          MArgument::MemBIS(helper_reg, helper_reg, 2, res_ty));
      return true;
    }
    case 11: {
      result.emplace_back(X86Subtype::lea, res_reg,
                          MArgument::MemBIS(base.reg, base.reg, 2, res_ty));
      result.emplace_back(X86Subtype::lea, res_reg,
                          MArgument::MemBIS(base.reg, res_reg.reg, 1, res_ty));
      return true;
    }
    case 12: {
      auto helper_arg = MArgument(helper_reg, res_ty);
      result.emplace_back(GBaseSubtype::mov, helper_arg, base);
      result.emplace_back(GArithSubtype::shl2, helper_arg, MArgument((u8)2));
      result.emplace_back(X86Subtype::lea, res_reg,
                          MArgument::MemBIS(helper_reg, helper_reg, 1, res_ty));
      return true;
    }
    case 13: {
      result.emplace_back(X86Subtype::lea, res_reg,
                          MArgument::MemBIS(base.reg, base.reg, 1, res_ty));
      result.emplace_back(X86Subtype::lea, res_reg,
                          MArgument::MemBIS(base.reg, res_reg.reg, 2, res_ty));
      return true;
    }
    case 14: {
      auto helper_arg = MArgument(helper_reg, res_ty);
      //       mov     eax, edi
      // lea     ecx, [rax + rax]
      // shl     eax, 4
      // sub     eax, ecx
      result.emplace_back(GBaseSubtype::mov, res_reg, base);
      result.emplace_back(X86Subtype::lea, helper_arg,
                          MArgument::MemBI(res_reg.reg, res_reg.reg, res_ty));
      result.emplace_back(GArithSubtype::shl2, res_reg, MArgument((u8)4));
      result.emplace_back(GArithSubtype::sub2, res_reg, helper_arg);
      return true;
    }
    case 15: {
      result.emplace_back(X86Subtype::lea, res_reg,
                          MArgument::MemBIS(base.reg, base.reg, 4, res_ty));
      result.emplace_back(
          X86Subtype::lea, res_reg,
          MArgument::MemBIS(res_reg.reg, res_reg.reg, 2, res_ty));
      return true;
    }
    case 16: {
      result.emplace_back(GBaseSubtype::mov, res_reg, base);
      result.emplace_back(GArithSubtype::shl2, res_reg, MArgument((u8)4));
      return true;
    }
  }

  // $1 = $0 * c
  // where $0 must be reg and C in [1,2,3,4,5,8,9]
  if (mul1More) {
    result.emplace_back(
        X86Subtype::lea, res_reg,
        MArgument::MemBIS(base.reg, base.reg, consti_val, res_ty));
  } else if (mul1Less) {
    result.emplace_back(X86Subtype::lea, res_reg,
                        MArgument::MemOIS(0, base.reg, consti_val, res_ty));
    result.emplace_back(GArithSubtype::sub2, res_reg, base);
  } else {
    result.emplace_back(X86Subtype::lea, res_reg,
                        MArgument::MemOIS(0, base.reg, consti_val, res_ty));
  }
  return true;
}

}  // namespace foptim::fmir
