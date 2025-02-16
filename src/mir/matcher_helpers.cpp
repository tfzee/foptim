#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "mir/matcher.hpp"

namespace foptim::fmir {
void setup_callargs(fir::Instr &call_instr, MatchResult &res,
                    ExtraMatchData &data) {
  TVec<MArgument> evaluated_args;
  for (size_t arg_id = 1; arg_id < call_instr->args.size(); arg_id++) {
    evaluated_args.push_back(
        valueToArg(call_instr->args[arg_id], res.result, data.alloc));
  }
  for (auto arg_value : evaluated_args) {
    res.result.emplace_back(Opcode::arg_setup, arg_value);
  }
}

MArgument valueToArgConst(fir::ValueR val, IRVec<MInstr> &res,
                          DumbRegAlloc &alloc) {

  ASSERT(val.is_constant());
  auto consti = val.as_constant();
  if (consti->is_int()) {
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
      utils::Debug << (i64)consti->as_int() << "\n";
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
    auto helper =
        MArgument{alloc.get_new_register(VRegInfo{Type::Int64}), Type::Int64};
    auto arg = MArgument::Mem(global->name, type_id);
    res.emplace_back(Opcode::lea, helper, arg);
    return helper;
  }
  if (consti->is_func()) {
    auto funcy = consti->as_func();
    auto arg = MArgument::Mem(funcy->getName(), Type::Int64);
    auto helper =
        MArgument(alloc.get_new_register(VRegInfo{Type::Int64}), Type::Int64);
    res.emplace_back(Opcode::lea, helper, arg);
    return helper;
  }
  if (consti->is_poisson()) {
    if (auto *v = std::get_if<fir::IntegerType>(&consti->type->type)) {
      switch (v->bitwidth) {
      case 8:
        return MArgument((u8)0);
      case 16:
        return MArgument((u16)0);
      case 32:
        return MArgument((u32)0);
      case 64:
        return MArgument((u64)0);
      default:
        utils::Debug << v->bitwidth << "\n";
        UNREACH();
      }

    } else if (auto *v = std::get_if<fir::FloatType>(&consti->type->type)) {
      return MArgument(0.0f);
    }
    utils::Debug << consti << " with type " << consti->type << "\n";
    UNREACH();
  }
  UNREACH();
}

MArgument valueToArg(fir::ValueR val, IRVec<MInstr> &res, DumbRegAlloc &alloc) {
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
      return MArgument::Mem(global->name, type_id);
    }
    if (constant->is_func()) {
      auto funcy = constant->as_func();
      return {funcy->getName()};
    }
    return {(u64)0, type_id};
  } else {
    return MArgument::Mem(alloc.get_register(val), type_id);
  }
  utils::Debug << val << "\n";
  ASSERT(false);
  std::abort();
}

} // namespace foptim::fmir
