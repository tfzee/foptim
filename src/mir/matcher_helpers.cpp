#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "mir/matcher.hpp"

namespace foptim::fmir {
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

  if (consti->is_float()) {
    if (val.get_type()->as_float() == 32) {
      return {(f32)consti->as_float()};
    }
    return {consti->as_float()};
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
