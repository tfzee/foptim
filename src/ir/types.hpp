#pragma once
#include "types_ref.hpp"
#include "utils/todo.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"
#include <cstdlib>
#include <typeinfo>
#include <utility>
#include <variant>

namespace foptim::fir {

class IntegerType {
public:
  u16 bitwidth;
  [[nodiscard]] constexpr u32 get_size() const { return (bitwidth + 7) / 8; }
  [[nodiscard]] constexpr bool eql(const IntegerType &other) const {
    return bitwidth == other.bitwidth;
  }
};

class FloatType {
public:
  u16 bitwidth;
  [[nodiscard]] constexpr u32 get_size() const { return (bitwidth + 7) / 8; }
  [[nodiscard]] constexpr bool eql(const FloatType &other) const {
    return bitwidth == other.bitwidth;
  }
};

class VoidType {
public:
  [[nodiscard]] constexpr u32 get_size() const { return 0; }
  [[nodiscard]] constexpr bool eql(const VoidType & /*unused*/) const {
    return true;
  }
};

class OpaquePointerType {
public:
  // TODO: address space
  [[nodiscard]] constexpr u32 get_size() const { return 8; }
  [[nodiscard]] constexpr bool eql(const OpaquePointerType & /*unused*/) const {
    return true;
  }
};

class FunctionType {
public:
  TypeR return_type;
  IRVec<TypeR> arg_types;
  [[nodiscard]] constexpr u32 get_size() const { return 8; }
  [[nodiscard]] bool eql(const FunctionType & /*unused*/) const {
    ASSERT(false);
    std::abort();
  }
};

class VectorType {
public:
  enum class SubType {
    Integer,
    Floating,
  };

  SubType type;
  // element/type bitwidth
  u16 bitwidth;
  u32 member_number;
  [[nodiscard]] constexpr u32 get_size() const {
    return ((bitwidth + 7) / 8) * member_number;
  }
  [[nodiscard]] constexpr bool eql(const VectorType &other) const {
    return member_number == other.member_number && type == other.type &&
           bitwidth == other.bitwidth;
  }
};

class AnyType {
public:
  using Union = std::variant<VoidType, IntegerType, FloatType,
                             OpaquePointerType, VectorType, FunctionType>;
  Union type;

  AnyType(Union t) : type(std::move(t)) {}
  AnyType(IntegerType t) : type(t) {}
  AnyType(FloatType t) : type(t) {}
  AnyType(FunctionType t) : type(t) {}
  AnyType(VoidType t) : type(t) {}
  AnyType(OpaquePointerType t) : type(t) {}

  //@returns the size of this type in bytes
  [[nodiscard]] u32 get_size() const {
    return std::visit([](auto &&v) { return v.get_size(); }, type);
  }

  [[nodiscard]] bool eql(const AnyType &other) const {
    return std::visit(
        [other](auto &&v1) {
          return std::visit(
              [&v1](auto &&v2) {
                if constexpr (typeid(v1) != typeid(v2)) {
                  return false;
                } else {
                  return v1.eql(v2);
                }
              },
              other.type);
        },
        type);
  }

  [[nodiscard]] const FunctionType &as_func_ty() const {
    if (const auto *ft = std::get_if<FunctionType>(&this->type)) {
      return *ft;
    }
    std::abort();
  }

  [[nodiscard]] u32 as_int() const {
    return std::get_if<IntegerType>(&type)->bitwidth;
  }
  [[nodiscard]] bool is_int() const {
    return std::holds_alternative<IntegerType>(type);
  }
  [[nodiscard]] u32 as_float() const {
    return std::get_if<FloatType>(&type)->bitwidth;
  }
  [[nodiscard]] bool is_float() const {
    return std::holds_alternative<FloatType>(type);
  }
  [[nodiscard]] bool is_void() const {
    return std::holds_alternative<VoidType>(type);
  }
  [[nodiscard]] bool is_ptr() const {
    return std::holds_alternative<OpaquePointerType>(type);
  }
  [[nodiscard]] const Union &get_raw() const { return type; }
};

} // namespace foptim::fir
