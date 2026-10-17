#pragma once
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

class FunctionType {
 public:
  // the result types, a function without result has the single entry void
  // (no other list contains void)
  IRVec<TypeR> return_types;
  IRVec<TypeR> arg_types;
  // type of the only result, void if there is none
  [[nodiscard]] TypeR return_type() const {
    ASSERT(return_types.size() == 1);
    return return_types[0];
  }
  [[nodiscard]] u32 n_results() const;
  [[nodiscard]] bool is_multi_return() const { return n_results() > 1; }
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
  [[nodiscard]] constexpr u32 get_bitwidth() const {
    return bitwidth * member_number;
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
  // structural hash that agrees with eql
  [[nodiscard]] u64 hash() const;

  [[nodiscard]] bool is_func() const { return ty == AnyTypeType::Function; }
  [[nodiscard]] bool is_int() const { return ty == AnyTypeType::Integer; }
  [[nodiscard]] bool is_float() const { return ty == AnyTypeType::Float; }
  [[nodiscard]] bool is_f32() const { return ty == AnyTypeType::Float && get_bitwidth() == 32; }
  [[nodiscard]] bool is_f64() const { return ty == AnyTypeType::Float && get_bitwidth() == 64; }
  [[nodiscard]] bool is_void() const { return ty == AnyTypeType::Void; }
  [[nodiscard]] bool is_ptr() const { return ty == AnyTypeType::Ptr; }
  [[nodiscard]] bool is_int_or_ptr() const { return is_int() || is_ptr(); }
  [[nodiscard]] bool is_vec() const { return ty == AnyTypeType::Vector; }

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
