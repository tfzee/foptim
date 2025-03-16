#include "context.hpp"
#include "utils/stable_vec_ref.hpp"
#include "utils/stable_vec_slot.hpp"
#include "utils/string.hpp"

namespace foptim::fir {

BasicBlock ContextData::copy(BasicBlock bb, V2VMap &subs, bool apply_subs) {
  auto res = storage.insert_bb(*bb.get_raw_ptr());
  subs.insert({ValueR{bb}, ValueR{res}});
  res->uses.clear();
  res->args.clear();
  for (u32 arg_id = 0; arg_id < bb->args.size(); arg_id++) {
    auto new_bb_arg = storage.insert_bb_arg(res, bb->args[arg_id]->get_type());
    subs.insert({ValueR{bb->args[arg_id]}, ValueR{new_bb_arg}});
    res.add_arg(new_bb_arg);
  }

  res->instructions.clear();
  for (auto &instr : bb->instructions) {
    auto new_instr = copy(instr);
    new_instr->uses.clear();
    if (apply_subs) {
      new_instr.substitute(subs);
    }
    subs.insert({ValueR{instr}, ValueR{new_instr}});
    res.push_instr(new_instr);

    new_instr->uses.clear();
    new_instr->args.clear();
    for (size_t i = 0; i < instr->args.size(); i++) {
      new_instr.add_arg(instr->args[i]);
    }
    new_instr->bbs.clear();
    for (auto &bb : instr->bbs) {
      new_instr.add_bb(bb.bb);
      for (auto &arg : bb.args) {
        new_instr.add_bb_arg(new_instr->bbs.size() - 1, arg);
      }
    }
  }
  return res;
}

BBArgument ContextData::copy(BBArgument bb_arg) {
  auto res =
      storage.insert_bb_arg({BasicBlock{BasicBlock::invalid()}, bb_arg->_type});
  res->uses.clear();
  return res;
}

Instr ContextData::copy(Instr instr) {
  auto res = storage.insert_instr(*instr.get_raw_ptr());
  return res;
}

void ContextData::print_stats() const {
  fmt::println("Funcs {}", storage.functions.size());
  fmt::println("Instr ");
  print_stats_vec(storage.storage_instr);
  fmt::println("BBs ");
  print_stats_vec(storage.basic_blocks);
  fmt::println("Constant ");
  print_stats_vec(storage.storage_constant);
  fmt::println("Type ");
  print_stats_vec(storage.storage_type);
  fmt::println("Global ");
  print_stats_vec(storage.storage_global);
}

Global ContextData::get_global(IRString name, size_t size_bytes) {
  return storage.insert_global({name, size_bytes});
}

FunctionR ContextData::get_function(IRString name) {
  if (!storage.functions.contains(name)) {
    fmt::println("Failed to find function '{}' from storage", name.c_str());
    ASSERT(false);
  }
  return &storage.functions.at(name);
}

FunctionR ContextData::create_function(IRString name, FunctionTypeR type) {
  storage.functions.insert({name, Function{this, name, type}});

  auto func = FunctionR(&storage.functions.at(name));
  auto init_bb = BasicBlock(storage.basic_blocks.push_back({func}));
  init_bb.verify_validness();
  func->append_bbr(init_bb);
  // func->set_entry_bbr(init_bb);
  const auto &arg_tys = type->as_func().arg_types;
  init_bb->args.reserve(arg_tys.size());
  for (auto arg_ty : arg_tys) {
    init_bb->args.push_back(storage.insert_bb_arg(init_bb, arg_ty));
  }
  return func;
}

bool ContextData::delete_function(IRStringRef delete_func) {
  if (storage.functions.contains(delete_func)) {
    storage.functions.erase(delete_func);
    return true;
  }

  return false;
}

bool ContextData::verify() const {
  for (const auto &[name, func] : storage.functions) {
    if (!func.verify()) {
      fmt::println("In Function: {}\n", name.c_str());
      return false;
    }
  }
  return true;
}

VoidTypeR ContextData::get_void_type() {
  static auto void_type = storage.insert_type(AnyType{});
  return void_type;
}
VoidTypeR ContextData::get_ptr_type() {
  static auto ptr_type = storage.insert_type(AnyType::Ptr());
  return ptr_type;
}

IntTypeR ContextData::get_float_type(u16 bitwidth) {
  static auto f32_type = storage.insert_type(FloatType{32});
  static auto f64_type = storage.insert_type(FloatType{64});
  if (bitwidth == 32) {
    return f32_type;
  }
  if (bitwidth == 64) {
    return f64_type;
  }
  return storage.insert_type(FloatType{bitwidth});
}

IntTypeR ContextData::get_int_type(u16 bitwidth) {
  static auto u1_type = storage.insert_type(IntegerType{1});
  static auto u8_type = storage.insert_type(IntegerType{8});
  static auto u16_type = storage.insert_type(IntegerType{16});
  static auto u32_type = storage.insert_type(IntegerType{32});
  static auto u64_type = storage.insert_type(IntegerType{64});
  if (bitwidth == 1) {
    return u1_type;
  }
  if (bitwidth == 8) {
    return u8_type;
  }
  if (bitwidth == 16) {
    return u16_type;
  }
  if (bitwidth == 32) {
    return u32_type;
  }
  if (bitwidth == 64) {
    return u64_type;
  }

  return storage.insert_type(IntegerType{bitwidth});
}

FunctionTypeR ContextData::get_func_ty(TypeR ret_type, IRVec<TypeR> args) {
  return storage.insert_type(FunctionType{ret_type, std::move(args)});
}

ConstantValueR ContextData::get_poisson_value(TypeR type) {
  return storage.insert_constant(ConstantValue(type));
}

ConstantValueR ContextData::try_reuse_constant(const ConstantValue &val) {
  for (auto *constant : storage.storage_constant._slot_slab_starts) {
    if (constant->used == utils::SlotState::Used) {
      if (constant->data.eql(val) &&
          constant->data.get_type() == val.get_type()) {
#ifdef SLOT_CHECK_GENERATION
        return ConstantValueR{utils::SRef{constant, constant->generation}};
#else
        return ConstantValueR{utils::SRef{constant, 0}};
#endif
      }
    }
  }
  return ConstantValueR{ConstantValueR::invalid()};
}

ConstantValueR ContextData::get_constant_value(FunctionR func) {
  const auto constant = ConstantValue(func, get_ptr_type());
  auto maybeR = try_reuse_constant(constant);
  if (maybeR.is_valid()) {
    return maybeR;
  }
  return storage.insert_constant(constant);
}

ConstantValueR ContextData::get_constant_value(f32 val, FloatTypeR ty) {
  const auto constant = ConstantValue(val, ty);
  auto maybeR = try_reuse_constant(constant);
  if (maybeR.is_valid()) {
    return maybeR;
  }
  return storage.insert_constant(constant);
}

ConstantValueR ContextData::get_constant_value(f64 val, FloatTypeR ty) {
  const auto constant = ConstantValue(val, ty);
  auto maybeR = try_reuse_constant(constant);
  if (maybeR.is_valid()) {
    return maybeR;
  }
  return storage.insert_constant(constant);
}

ConstantValueR ContextData::get_constant_value(u64 val, IntTypeR ty) {
  const auto constant = ConstantValue(val, ty);
  auto maybeR = try_reuse_constant(constant);
  if (maybeR.is_valid()) {
    return maybeR;
  }
  return storage.insert_constant(constant);
}

ConstantValueR ContextData::get_constant_value(i64 val, IntTypeR ty) {
  const auto constant = ConstantValue(val, ty);
  auto maybeR = try_reuse_constant(constant);
  if (maybeR.is_valid()) {
    return maybeR;
  }
  return storage.insert_constant(constant);
}

ConstantValueR ContextData::get_constant_value(i32 val, IntTypeR ty) {
  const auto constant = ConstantValue((i64)val, ty);
  auto maybeR = try_reuse_constant(constant);
  if (maybeR.is_valid()) {
    return maybeR;
  }
  return storage.insert_constant(constant);
}

ConstantValueR ContextData::get_constant_value(i128 val, IntTypeR ty) {
  const auto constant = ConstantValue(val, ty);
  auto maybeR = try_reuse_constant(constant);
  if (maybeR.is_valid()) {
    return maybeR;
  }
  return storage.insert_constant(constant);
}

ConstantValueR ContextData::get_constant_value(u32 val, IntTypeR ty) {
  const auto constant = ConstantValue((u64)val, ty);
  auto maybeR = try_reuse_constant(constant);
  if (maybeR.is_valid()) {
    return maybeR;
  }
  return storage.insert_constant(constant);
}

ConstantValueR ContextData::get_constant_value(Global glob) {
  static auto global_ptr_typee = storage.insert_type(AnyType::Ptr());

  const auto constant = ConstantValue(glob, global_ptr_typee);
  auto maybeR = try_reuse_constant(constant);
  if (maybeR.is_valid()) {
    return maybeR;
  }
  return storage.insert_constant(constant);
}

} // namespace foptim::fir
