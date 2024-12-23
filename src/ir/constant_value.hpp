#pragma once
#include "ir/function_ref.hpp"
#include "ir/global.hpp"
#include "ir/types.hpp"
#include "types_ref.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"
#include <cstdlib>
#include <variant>

namespace foptim::fir {
struct ConstantValue;
class ValueR;

struct IntValue {
  u64 data;
  bool operator==(const IntValue &other) const { return data == other.data; }
};

struct FloatValue {
  f64 data;
  bool operator==(const FloatValue &other) const { return data == other.data; }
};

struct FunctionPtr {
  FunctionR func;
  bool operator==(const FunctionPtr &other) const { return func == other.func; }
};

struct GlobalPointer {
  Global glob;
  bool operator==(const GlobalPointer &other) const {
    return glob == other.glob;
  }
};

struct VectorValue {
  IRVec<ConstantValue> members;
  bool operator==(const VectorValue &other) const;
};

struct ConstantValue {
  std::variant<IntValue, FloatValue, GlobalPointer, FunctionPtr, VectorValue>
      value;
  TypeR type;

  constexpr ConstantValue(u64 v, TypeR typee)
      : value(IntValue{v}), type(typee) {}

  constexpr ConstantValue(VectorValue v, TypeR typee) : value(v), type(typee) {}

  constexpr ConstantValue(f64 v, TypeR typee)
      : value(FloatValue{v}), type(typee) {}

  constexpr ConstantValue(f32 v, TypeR typee)
      : value(FloatValue{v}), type(typee) {}

  constexpr ConstantValue(Global g, TypeR typee)
      : value(GlobalPointer{g}), type(typee) {}

  constexpr ConstantValue(FunctionR f, TypeR typee)
      : value(FunctionPtr{f}), type(typee) {}

  [[nodiscard]] bool is_global() const {
    return std::holds_alternative<GlobalPointer>(value);
  }
  [[nodiscard]] bool is_int() const {
    return std::holds_alternative<IntValue>(value);
  }

  [[nodiscard]] bool is_float() const {
    return std::holds_alternative<FloatValue>(value);
  }

  [[nodiscard]] bool is_func() const {
    return std::holds_alternative<FunctionPtr>(value);
  }

  [[nodiscard]] FunctionR as_func() const {
    if (const auto *res = std::get_if<FunctionPtr>(&value)) {
      return res->func;
    }
    TODO("UNREACH");
  }

  [[nodiscard]] f64 as_float() const {
    if (const auto *res = std::get_if<FloatValue>(&value)) {
      if (type->as_float() == 32) {
        return (f32)res->data;
      }
      return res->data;
    }
    TODO("UNREACH");
  }

  [[nodiscard]] u64 as_int() const {
    if (const auto *res = std::get_if<IntValue>(&value)) {
      // u32 bitwidth = type->as_int();
      // const u64 mask = ((u64)1 << bitwidth) - 1;
      return res->data; // & mask;
    }
    TODO("UNREACH");
  }

  [[nodiscard]] Global as_global() const {
    if (const auto *res = std::get_if<GlobalPointer>(&value)) {
      return res->glob;
    }
    std::abort();
  }
  [[nodiscard]] TypeR get_type() const;
  [[nodiscard]] bool eql(const ConstantValue &) const;
};

} // namespace foptim::fir
