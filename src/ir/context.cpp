#include "context.hpp"
#include "utils/stable_vec_ref.hpp"

namespace foptim::fir {

VoidTypeR ContextData::get_void_type() {
  static auto void_type = storage.insert_type(VoidType{});
  return void_type;
}
VoidTypeR ContextData::get_ptr_type() {
  static auto ptr_type = storage.insert_type(OpaquePointerType{});
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
    if (constant->used) {
      if (constant->data.eql(val) &&
          constant->data.get_type() == val.get_type()) {
        return ConstantValueR{utils::SRef{constant, constant->generation}};
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
  const auto constant = ConstantValue((u64)val, ty);
  auto maybeR = try_reuse_constant(constant);
  if (maybeR.is_valid()) {
    return maybeR;
  }
  return storage.insert_constant(constant);
}

ConstantValueR ContextData::get_constant_value(i32 val, IntTypeR ty) {
  const auto constant = ConstantValue((u64)(i64)val, ty);
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
  static auto global_ptr_typee =
      storage.insert_type(AnyType{OpaquePointerType()});

  const auto constant = ConstantValue(glob, global_ptr_typee);
  auto maybeR = try_reuse_constant(constant);
  if (maybeR.is_valid()) {
    return maybeR;
  }
  return storage.insert_constant(constant);
}

} // namespace foptim::fir
