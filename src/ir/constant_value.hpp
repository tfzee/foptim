#pragma once
#include "ir/global.hpp"
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

struct GlobalPointer {
  Global glob;
  bool operator==(const GlobalPointer &other) const {
    return glob == other.glob;
  }
};

struct VectorValue {
  FVec<ConstantValue> members;
  bool operator==(const VectorValue &other) const;
};

struct ConstantValue {
  std::variant<IntValue, FloatValue, GlobalPointer, VectorValue> value;
  TypeR type;

  constexpr ConstantValue(u64 v, TypeR typee)
      : value(IntValue{v}), type(typee) {}

  constexpr ConstantValue(Global g, TypeR typee) : value(GlobalPointer{g}), type(typee) {}

  bool is_global() const {
    return std::holds_alternative<GlobalPointer>(value);
  }
  bool is_int() const { return std::holds_alternative<IntValue>(value); }

  u64 as_int() const {
    if (auto *res = std::get_if<IntValue>(&value)) {
      return res->data;
    } else {
      std::abort();
    }
  }

  Global as_global() const {
    if (auto *res = std::get_if<GlobalPointer>(&value)) {
      return res->glob;
    } else {
      std::abort();
    }
  }
  TypeR get_type() const;
  bool eql(const ConstantValue &) const;
};

} // namespace foptim::fir
