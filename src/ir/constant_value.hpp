#pragma once
#include "ir/constant_value_ref.hpp"
#include "ir/function_ref.hpp"
#include "ir/global.hpp"
#include "types_ref.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::fir {

struct IntValue {
  i128 data;
  [[gnu::always_inline]] constexpr bool operator==(
      const IntValue &other) const {
    return data == other.data;
  }
};

struct FloatValue {
  f64 data;

  constexpr FloatValue(f64 d) : data(d) {}
  constexpr FloatValue(f32 d)
      : data(std::bit_cast<f64>((u64)std::bit_cast<u32>(d))) {}
  [[gnu::always_inline]] constexpr bool operator==(
      const FloatValue &other) const {
    return data == other.data;
  }
};

struct FunctionPtr {
  FunctionR func;
  [[gnu::always_inline]] constexpr bool operator==(
      const FunctionPtr &other) const {
    return func == other.func;
  }
};

struct ConstantStruct {
  IRVec<ConstantValue> v;
  bool operator==(const ConstantStruct &other) const;
};

struct GlobalPointer {
  Global glob;
  [[gnu::always_inline]] constexpr bool operator==(
      const GlobalPointer &other) const {
    return glob == other.glob;
  }
};

struct VectorValue {
  IRVec<ConstantValueR> members;
  bool operator==(const VectorValue &other) const;
};

enum class ConstantType : u8 {
  // just a invalid value similarly to llvms poisson value it is some undefined
  // but constant value
  PoisonValue = 0,
  IntValue,
  FloatValue,
  VectorValue,
  GlobalPtr,
  FuncPtr,
  NullPtr,
  ConstantStruct,
};

struct ConstantValue {
  TypeR type;
  union {
    struct {
      ConstantType ty;
    };
    struct {
      ConstantType _ty;
      VectorValue v;
    } vec_u;
    struct {
      ConstantType _ty;
      IntValue v;
    } int_u;
    struct {
      ConstantType _ty;
      FloatValue v;
    } float_u;
    struct {
      ConstantType _ty;
      GlobalPointer v;
    } gp_u;
    struct {
      ConstantType _ty;
      FunctionPtr v;
    } fup_u;
    struct {
      ConstantType _ty;
      ConstantStruct v;
    } stru_u;
  };

  constexpr ConstantValue(TypeR typee)
      : type(typee), ty(ConstantType::PoisonValue) {}
  ~ConstantValue();
  ConstantValue(const ConstantValue &);
  ConstantValue &operator=(const ConstantValue &);

  constexpr ConstantValue(i128 v, TypeR typee)
      : type(typee), int_u({._ty = ConstantType::IntValue, .v = IntValue{v}}) {}

  constexpr ConstantValue(i64 v, TypeR typee)
      : type(typee), int_u({._ty = ConstantType::IntValue, .v = IntValue{v}}) {}

  constexpr ConstantValue(u64 v, TypeR typee)
      : type(typee), int_u({._ty = ConstantType::IntValue, .v = IntValue{v}}) {}

  constexpr ConstantValue(f64 v, TypeR typee)
      : type(typee),
        float_u({._ty = ConstantType::FloatValue, .v = FloatValue{v}}) {}

  constexpr ConstantValue(f32 v, TypeR typee)
      : type(typee),
        float_u({._ty = ConstantType::FloatValue, .v = FloatValue{v}}) {}

  constexpr ConstantValue(Global g, TypeR typee)
      : type(typee),
        gp_u({._ty = ConstantType::GlobalPtr, .v = GlobalPointer{g}}) {}

  constexpr ConstantValue(FunctionR f, TypeR typee)
      : type(typee),
        fup_u({._ty = ConstantType::FuncPtr, .v = FunctionPtr{f}}) {}

  constexpr ConstantValue(ConstantStruct stru, TypeR typee)
      : type(typee), stru_u({._ty = ConstantType::ConstantStruct, .v = stru}) {}

  constexpr ConstantValue(IRVec<ConstantValueR> members, TypeR typee)
      : type(typee),
        vec_u({._ty = ConstantType::VectorValue,
               .v = VectorValue{std::move(members)}}) {}

  static ConstantValue null_ptr(TypeR typee) {
    auto c = ConstantValue(typee);
    c.ty = ConstantType::NullPtr;
    return c;
  }

  std::optional<fir::ConstantValueR> bit_cast(Context& ctx, fir::TypeR target_type);

  void add_usage(Use u);
  [[nodiscard]] size_t get_n_uses() const;
  void remove_usage(Use u, bool verify = true);
  void replace_all_uses(ValueR);
  [[nodiscard]] IRVec<Use> *get_uses();
  [[nodiscard]] const IRVec<Use> *get_uses() const;

  [[nodiscard]] bool is_valid() const;

  [[nodiscard]] constexpr bool is_global() const {
    return ty == ConstantType::GlobalPtr;
  }
  [[nodiscard]] constexpr bool is_int() const {
    return ty == ConstantType::IntValue;
  }

  [[nodiscard]] constexpr bool is_float() const {
    return ty == ConstantType::FloatValue;
  }

  [[nodiscard]] constexpr bool is_null() const {
    return ty == ConstantType::NullPtr;
  }

  [[nodiscard]] constexpr bool is_func() const {
    return ty == ConstantType::FuncPtr;
  }

  [[nodiscard]] constexpr bool is_poison() const {
    return ty == ConstantType::PoisonValue;
  }

  [[nodiscard]] constexpr bool is_vec() const {
    return ty == ConstantType::VectorValue;
  }

  [[nodiscard]] constexpr FunctionR as_func() const {
    ASSERT(is_func());
    return fup_u.v.func;
  }

  [[nodiscard]] constexpr const VectorValue &as_vec() const {
    ASSERT(is_vec());
    return vec_u.v;
  }

  [[nodiscard]] constexpr f32 as_f32() const {
    ASSERT(is_float());
    return std::bit_cast<f32>((u32)std::bit_cast<u64>(float_u.v.data));
  }

  [[nodiscard]] constexpr f64 as_f64() const {
    ASSERT(is_float());
    return float_u.v.data;
  }

  // OUTDATED USE as_f32 and as_f64
  [[nodiscard]] constexpr f64 as_float() const {
    ASSERT(is_float());
    return float_u.v.data;
  }

  [[nodiscard]] constexpr i128 as_int() const {
    if (is_null()){
      return 0;
    }
    if (!is_int()) {
      fmt::println("{}", *this);
      TODO("");
    }
    return int_u.v.data;
  }

  [[nodiscard]] constexpr Global as_global() const {
    ASSERT(is_global());
    return gp_u.v.glob;
  }
  [[nodiscard]] TypeR get_type() const;
  [[nodiscard]] bool eql(const ConstantValue &) const;
};

}  // namespace foptim::fir
