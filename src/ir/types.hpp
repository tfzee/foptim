#pragma once
#include "types_ref.hpp"
#include "utils/todo.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"
#include <cstdlib>
#include <typeinfo>
#include <variant>

namespace foptim::fir {

class IntegerType {
public:
  u16 bitwidth;
  constexpr u32 get_size() const { return (bitwidth + 7) / 8; }
  bool eql(const IntegerType &other) const {
    return bitwidth == other.bitwidth;
  }
};

class FloatType {
public:
  u16 bitwidth;
  constexpr u32 get_size() const { return (bitwidth + 7) / 8; }
  bool eql(const FloatType &) const {
    ASSERT(false);
    std::abort();
  }
};

class VoidType {
public:
  constexpr u32 get_size() const { return 0; }
  bool eql(const VoidType &) const { return true; }
};

class OpaquePointerType {
public:
  // TODO: address space
  constexpr u32 get_size() const { return 8; }
  bool eql(const OpaquePointerType &) const { return true; }
};

class FunctionType {
public:
  TypeR return_type;
  FVec<TypeR> arg_types;
  constexpr u32 get_size() const { return 8; }
  bool eql(const FunctionType &) const {
    ASSERT(false);
    std::abort();
  }
};

class VectorType {

public:
  TypeR member_type;
  u32 member_number;
  u32 get_size() const;

  bool eql(const VectorType &) const {
    // TODO: impl
    ASSERT(false);
    return false;
  }
};

class AnyType {
  using Union = std::variant<VoidType, IntegerType, FloatType,
                             OpaquePointerType, VectorType, FunctionType>;
  Union type;

public:
  AnyType(Union t) : type(t) {}
  AnyType(IntegerType t) : type(t) {}
  AnyType(FunctionType t) : type(t) {}
  AnyType(VoidType t) : type(t) {}
  AnyType(OpaquePointerType t) : type(t) {}

  u32 get_size() const {
    return std::visit([](auto &&v) { return v.get_size(); }, type);
  }

  bool eql(const AnyType &other) const {
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

  const FunctionType &as_func_ty() const {
    if (const auto *ft = std::get_if<FunctionType>(&this->type)) {
      return *ft;
    } else {
      std::abort();
    }
  }

  u32 as_int() const { return std::get_if<IntegerType>(&type)->bitwidth; }
  bool is_int() const { return std::holds_alternative<IntegerType>(type); }
  bool is_void() const { return std::holds_alternative<VoidType>(type); }
  bool is_ptr() const {
    return std::holds_alternative<OpaquePointerType>(type);
  }
  const Union &get_raw() const { return type; }
};

} // namespace foptim::fir
