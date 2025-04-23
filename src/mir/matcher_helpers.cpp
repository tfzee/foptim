#include "ir/basic_block_arg.hpp"
#include "ir/instruction_data.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "mir/matcher.hpp"

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
      for (auto use : arg->uses) {
        if (use.user->parent != def_bb) {
          safe_to_not_copy = false;
          break;
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

MArgument setup_callarg(fir::ValueR arg, MatchResult &res,
                        ExtraMatchData &data) {
  if (!arg.is_instr()) {
    return valueToArg(arg, res.result, data.alloc);
  }
  auto arg_instr = arg.as_instr();
  if (arg_instr->is(fir::InstrType::BinaryInstr) &&
      arg_instr->subtype == (u32)fir::BinaryInstrSubType::IntAdd) {
    auto res_type = convert_type(arg_instr->get_type());
    auto v0 = valueToArg(arg_instr->args[0], res.result, data.alloc);
    auto end_reg = data.alloc.get_new_register(res_type);
    auto end_argument = MArgument{end_reg, res_type};

    if (arg_instr->args[1].is_constant()) {
      auto int_const = arg_instr->args[1].as_constant()->as_int();
      if (v0.isLabel()) {
        res.result.emplace_back(
            Opcode::lea, end_argument,
            MArgument::MemLO(v0.label, int_const, res_type));
      } else if (v0.isReg()) {
        res.result.emplace_back(Opcode::lea, end_argument,
                                MArgument::MemOB(int_const, v0.reg, res_type));
      } else {

        return valueToArg(arg, res.result, data.alloc);
      }
      return end_argument;
    }

    auto v1 = valueToArg(arg_instr->args[1], res.result, data.alloc);
    if ((v0.isReg() && v0.reg.is_concrete()) ||
        (v1.isReg() && v1.reg.is_concrete())) {
      return valueToArg(arg, res.result, data.alloc);
    }
    if (v0.isReg() && v1.isReg()) {
      res.result.emplace_back(Opcode::lea, end_argument,
                              MArgument::MemBI(v0.reg, v1.reg, res_type));
    } else {
      return valueToArg(arg, res.result, data.alloc);
    }
    return end_argument;
  }

  return valueToArg(arg, res.result, data.alloc);
}

void setup_callargs(fir::Instr &call_instr, MatchResult &res,
                    ExtraMatchData &data) {
  // fmt::println("Instr: {}", call_instr);
  TVec<MArgument> evaluated_args;
  for (size_t arg_id = 1; arg_id < call_instr->args.size(); arg_id++) {
    evaluated_args.push_back(
        setup_callarg(call_instr->args[arg_id], res, data));
  }
  for (auto arg_value : evaluated_args) {
    res.result.emplace_back(Opcode::arg_setup, arg_value);
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
      return {(u8)std::bit_cast<u64>((i64)consti->as_int())};
    case 8:
      return {(u8)std::bit_cast<u64>((i64)consti->as_int())};
    case 16:
      return {(u16)std::bit_cast<u64>((i64)consti->as_int())};
    case 32:
      return {(u32)std::bit_cast<u64>((i64)consti->as_int())};
    case 64:
      return {(u64)std::bit_cast<u64>((i64)consti->as_int())};
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
    // TODO: idk if i64 is right here
    Type type_id = convert_type(val.get_type());
    auto arg = MArgument::MemL(global->name.c_str(), type_id);
    auto helper = MArgument{alloc.get_new_register(Type::Int64), Type::Int64};
    res.emplace_back(Opcode::lea, helper, arg);
    return helper;
  }
  if (consti->is_func()) {
    auto funcy = consti->as_func();
    auto arg = MArgument::MemL(funcy->getName().c_str(), Type::Int64);
    auto helper = MArgument(alloc.get_new_register(Type::Int64), Type::Int64);
    res.emplace_back(Opcode::lea, helper, arg);
    return helper;
  }
  if (consti->is_poison()) {
    switch (consti->type->ty) {
    case fir::AnyTypeType::Integer:
      switch (consti->type->as_int()) {
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
    case fir::AnyTypeType::Float:
      return {0.0F};
    case fir::AnyTypeType::Vector:
    case fir::AnyTypeType::Ptr:
    case fir::AnyTypeType::Function:
    case fir::AnyTypeType::Void:
    case fir::AnyTypeType::Struct:
      fmt::println("{} with type {}", consti, consti->type);
      UNREACH();
      break;
    }
  }
  UNREACH();
}

MArgument valueToArg(fir::ValueR val, TVec<MInstr> &res, DumbRegAlloc &alloc) {
  if (val.is_constant()) {
    return valueToArgConst(val, res, alloc);
  }
  Type type_id = convert_type(val.get_type());
  return {alloc.get_register(val), type_id};
}

MArgument valueToArgPtr(fir::ValueR val, Type type_id, DumbRegAlloc &alloc) {
  if (val.is_constant()) {
    auto constant = val.as_constant();
    if (constant->is_global()) {
      auto global = constant->as_global();
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
    TODO("unreach?");
    // return {(u64)0, :wype_id};
  } else {
    return MArgument::MemB(alloc.get_register(val), type_id);
  }
  fmt::println("{}", val);
  ASSERT(false);
  std::abort();
}

void setup_va_start(fir::Instr &call_instr, MatchResult &res,
                    ExtraMatchData &data) {
  auto ptr_arg = valueToArg(call_instr->args[1], res.result, data.alloc);
  // TODO: handle other cases aswell
  ASSERT(ptr_arg.isReg());

  auto n_int_args = 0;
  auto n_vec_args = 0;
  //TODO: idk about this
  for (auto arg: data.func.args) {
    if(arg.is_vec_reg()){
      n_vec_args += 1;
    }else{
      n_int_args += 1;
    }
  }

  res.result.emplace_back(Opcode::mov,
                          MArgument::MemOB(0, ptr_arg.reg, Type::Int32),
                          MArgument((u32)8 * n_int_args));
  res.result.emplace_back(Opcode::mov,
                          MArgument::MemOB(4, ptr_arg.reg, Type::Int32),
                          MArgument((u32)48 + 16 * n_vec_args));
  res.result.emplace_back(Opcode::mov,
                          MArgument::MemOB(8, ptr_arg.reg, Type::Int64),
                          MArgument(VReg::RBP(), Type::Int64));
  res.result.emplace_back(Opcode::add2,
                          MArgument::MemOB(8, ptr_arg.reg, Type::Int64),
                          MArgument((u16)16));
  res.result.emplace_back(Opcode::mov,
                          MArgument::MemOB(16, ptr_arg.reg, Type::Int64),
                          MArgument(VReg::RBP(), Type::Int64));
  res.result.emplace_back(Opcode::sub2,
                          MArgument::MemOB(16, ptr_arg.reg, Type::Int64),
                          MArgument((u16)176));
}

void setup_va_end(fir::Instr &call_instr, MatchResult &res,
                  ExtraMatchData &data) {
  (void)call_instr;
  (void)res;
  (void)data;
}

} // namespace foptim::fmir
