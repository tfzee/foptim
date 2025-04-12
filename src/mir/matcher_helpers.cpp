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

void setup_callargs(fir::Instr &call_instr, MatchResult &res,
                    ExtraMatchData &data) {
  // fmt::println("Instr: {}", call_instr);
  TVec<MArgument> evaluated_args;
  for (size_t arg_id = 1; arg_id < call_instr->args.size(); arg_id++) {
    evaluated_args.push_back(
        valueToArg(call_instr->args[arg_id], res.result, data.alloc));
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

} // namespace foptim::fmir
