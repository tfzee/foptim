#pragma once
#include <cstdlib>

#include "types_ref.hpp"
#include "utils/todo.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::fir {

class IntegerType {
 public:
  u16 bitwidth;
  [[nodiscard]] constexpr u32 get_size() const { return (bitwidth + 7) / 8; }
  [[nodiscard]] constexpr u32 get_align() const { return (bitwidth + 7) / 8; }
  [[nodiscard]] constexpr bool eql(const IntegerType &other) const {
    return bitwidth == other.bitwidth;
  }
};

class FloatType {
 public:
  u16 bitwidth;
  [[nodiscard]] constexpr u32 get_size() const { return (bitwidth + 7) / 8; }
  [[nodiscard]] constexpr u32 get_align() const { return (bitwidth + 7) / 8; }
  [[nodiscard]] constexpr bool eql(const FloatType &other) const {
    return bitwidth == other.bitwidth;
  }
};

class StructType {
 public:
  struct StructElem {
    u64 offset;
    TypeR ty;
  };
  IRVec<StructElem> elems;
  [[nodiscard]] u32 get_size() const;
  [[nodiscard]] u32 get_bitwidth() const;
  [[nodiscard]] u32 get_align() const;
  [[nodiscard]] constexpr bool eql(const StructType &other) const {
    if (elems.size() != other.elems.size()) {
      return false;
    }
    for (size_t i = 0; i < elems.size(); i++) {
      if (elems[i].ty != other.elems[i].ty ||
          elems[i].offset != elems[i].offset) {
        return false;
      }
    }
    return true;
  }
};

class FunctionType {
 public:
  TypeR return_type;
  IRVec<TypeR> arg_types;
  [[nodiscard]] constexpr u32 get_size() const { return 8; }
  [[nodiscard]] constexpr u32 get_bitwidth() const { return 8 * 8; }
  [[nodiscard]] constexpr u32 get_align() const { return 8; }
  [[nodiscard]] bool eql(const FunctionType &other) const;
};

class VectorType {
 public:
  enum class SubType : u8 {
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
  [[nodiscard]] constexpr u32 get_align() const {
    return ((bitwidth + 7) / 8) * member_number;
  }
  [[nodiscard]] constexpr bool eql(const VectorType &other) const {
    return member_number == other.member_number && type == other.type &&
           bitwidth == other.bitwidth;
  }
};

enum class AnyTypeType : u8 {
  Void = 0,
  Integer,
  Float,
  Ptr,
  Function,
  Vector,
  Struct,
};

class AnyType {
 public:
  union {
    AnyTypeType ty;
    struct {
      AnyTypeType _ty;
      IntegerType v;
    } int_u;
    struct {
      AnyTypeType _ty;
      FloatType v;
    } float_u;
    struct {
      AnyTypeType _ty;
      VectorType v;
    } vec_u;
    struct {
      AnyTypeType _ty;
      FunctionType v;
    } func_u;
    struct {
      AnyTypeType _ty;
      StructType v;
    } struct_u;
  };

  consteval AnyType() : ty(AnyTypeType::Void) {}
  ~AnyType();
  AnyType(const AnyType &);
  AnyType(AnyType &&) noexcept;
  AnyType &operator=(const AnyType &);
  AnyType &operator=(AnyType &&) noexcept;
  // constexpr AnyType(VoidType t) : ty(AnyTypeType::Void) {}
  constexpr AnyType(IntegerType t)
      : int_u({._ty = AnyTypeType::Integer, .v = t}) {}
  constexpr AnyType(FloatType t)
      : float_u({._ty = AnyTypeType::Float, .v = t}) {}
  constexpr AnyType(FunctionType t)
      : func_u({._ty = AnyTypeType::Function, .v = t}) {}
  constexpr AnyType(VectorType t)
      : vec_u({._ty = AnyTypeType::Vector, .v = t}) {}
  constexpr AnyType(StructType t)
      : struct_u({._ty = AnyTypeType::Struct, .v = t}) {}

  static AnyType Ptr() {
    auto out = AnyType();
    out.ty = AnyTypeType::Ptr;
    return out;
  }

  //@returns the size of this type in bytes
  [[nodiscard]] u32 get_size() const;
  [[nodiscard]] u32 get_bitwidth() const;
  [[nodiscard]] u32 get_align() const;
  [[nodiscard]] bool eql(const AnyType &other) const;

  [[nodiscard]] bool is_struct() const { return ty == AnyTypeType::Struct; }
  [[nodiscard]] bool is_func() const { return ty == AnyTypeType::Function; }
  [[nodiscard]] bool is_int() const { return ty == AnyTypeType::Integer; }
  [[nodiscard]] bool is_float() const { return ty == AnyTypeType::Float; }
  [[nodiscard]] bool is_void() const { return ty == AnyTypeType::Void; }
  [[nodiscard]] bool is_ptr() const { return ty == AnyTypeType::Ptr; }
  [[nodiscard]] bool is_vec() const { return ty == AnyTypeType::Vector; }

  [[nodiscard]] const StructType &as_struct() const {
    ASSERT(is_struct());
    return struct_u.v;
  }

  [[nodiscard]] const VectorType &as_vec() const {
    ASSERT(is_vec());
    return vec_u.v;
  }

  [[nodiscard]] const FunctionType &as_func() const {
    ASSERT(is_func());
    return func_u.v;
  }

  [[nodiscard]] u32 as_int() const {
    ASSERT(is_int());
    return int_u.v.bitwidth;
  }

  [[nodiscard]] u32 as_float() const {
    ASSERT(is_float());
    return float_u.v.bitwidth;
  }
};

}  // namespace foptim::fir
