#pragma once
#include "ir/function_ref.hpp"
#include "ir/global.hpp"
#include "ir/types.hpp"
#include "types_ref.hpp"
#include "utils/APInt.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"
#include <cstdlib>
#include <variant>

namespace foptim::fir {

struct IntValue {
  i128 data;
  constexpr bool operator==(const IntValue &other) const {
    return data == other.data;
  }
};

struct FloatValue {
  f64 data;
  constexpr bool operator==(const FloatValue &other) const {
    return data == other.data;
  }
};

struct FunctionPtr {
  FunctionR func;
  constexpr bool operator==(const FunctionPtr &other) const {
    return func == other.func;
  }
};

struct GlobalPointer {
  Global glob;
  constexpr bool operator==(const GlobalPointer &other) const {
    return glob == other.glob;
  }
};

// just a invalid value similarly to llvms poisson value it is some undefined
// but constant value
struct PoisonValue {
  constexpr bool operator==(const PoisonValue & /*_*/) const { return true; }
};

struct VectorValue {
  IRVec<ConstantValue> members;
  bool operator==(const VectorValue &other) const;
};

struct ConstantValue {
  std::variant<IntValue, FloatValue, GlobalPointer, FunctionPtr, VectorValue,
               PoisonValue>
      value;
  TypeR type;

  // Poisson value
  constexpr ConstantValue(TypeR typee) : value(PoisonValue{}), type(typee) {}

  constexpr ConstantValue(i128 v, TypeR typee)
      : value(IntValue{v}), type(typee) {}

  constexpr ConstantValue(i64 v, TypeR typee)
      : value(IntValue{v}), type(typee) {}

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

  [[nodiscard]] bool is_valid() const {
    if (!type.is_valid()) {
      fmt::println("Invalid type\n");
      return false;
    }
    return true;
  }

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

  [[nodiscard]] bool is_poison() const {
    return std::holds_alternative<PoisonValue>(value);
  }

  [[nodiscard]] FunctionR as_func() const {
    if (const auto *res = std::get_if<FunctionPtr>(&value)) {
      return res->func;
    }
    UNREACH();
  }

  [[nodiscard]] f64 as_float() const {
    if (const auto *res = std::get_if<FloatValue>(&value)) {
      if (type->as_float() == 32) {
        return (f32)res->data;
      }
      return res->data;
    }
    UNREACH();
  }

  [[nodiscard]] i128 as_int() const {
    if (const auto *res = std::get_if<IntValue>(&value)) {
      return res->data;
    }
    UNREACH();
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
