#pragma once
#include <ankerl/unordered_dense.h>

#include <cassert>

#include "basic_block_ref.hpp"
#include "instruction.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/use.hpp"
#include "types_ref.hpp"

namespace foptim::fir {

enum class ValueType {
  InvalidValue = 0,
  Instr,
  BasicBlock,
  BBArg,
  ConstantValueR,
};

class ValueR {
 public:
  ValueType ty;
  union {
    Instr instr;
    BasicBlock bb;
    BBArgument bb_arg;
    ConstantValueR const_val;
  };

  void add_usage(Use u);
  [[nodiscard]] size_t get_n_uses() const;
  void remove_usage(Use u, bool verify = true);
  void replace_all_uses(ValueR);
  [[nodiscard]] IRVec<Use> *get_uses();
  [[nodiscard]] const IRVec<Use> *get_uses() const;
  [[nodiscard]] TypeR get_type() const;

  consteval ValueR() : ty(ValueType::InvalidValue) {}
  explicit constexpr ValueR(ConstantValueR v)
      : ty(ValueType::ConstantValueR), const_val(v) {}
  explicit constexpr ValueR(Instr v) : ty(ValueType::Instr), instr(v) {}
  explicit constexpr ValueR(BasicBlock v) : ty(ValueType::BasicBlock), bb(v) {}
  explicit constexpr ValueR(BBArgument v) : ty(ValueType::BBArg), bb_arg(v) {}

  [[nodiscard]] bool eql(const ValueR &other) const;

  [[nodiscard]] bool operator==(const ValueR &other) const;

  [[nodiscard]] constexpr bool is_constant() const {
    return ty == ValueType::ConstantValueR;
  }
  [[nodiscard]] constexpr bool is_constant_func() const {
    return ty == ValueType::ConstantValueR && const_val->is_func();
  }
  [[nodiscard]] constexpr bool is_const_int(i128 v) const {
    return ty == ValueType::ConstantValueR && const_val->is_int() &&
           const_val->as_int() == v;
  }
  [[nodiscard]] constexpr bool is_constant_int() const {
    return ty == ValueType::ConstantValueR && const_val->is_int();
  }
  [[nodiscard]] constexpr bool is_poison() const {
    return ty == ValueType::ConstantValueR && const_val->is_poison();
  }
  [[nodiscard]] constexpr bool is_constant_float() const {
    return ty == ValueType::ConstantValueR && const_val->is_float();
  }
  [[nodiscard]] constexpr bool is_instr() const {
    return ty == ValueType::Instr;
  }
  [[nodiscard]] constexpr bool is_bb() const {
    return ty == ValueType::BasicBlock;
  }
  [[nodiscard]] constexpr bool is_bb_arg() const {
    return ty == ValueType::BBArg;
  }
  [[nodiscard]] constexpr bool is_invalid() const {
    return ty == ValueType::InvalidValue;
  }

  [[nodiscard]] bool is_valid(bool check_refs) const;

  [[nodiscard]] constexpr ConstantValueR try_constant() const {
    if (is_constant()) {
      return const_val;
    }
    return ConstantValueR{ConstantValueR::invalid()};
  }

  [[nodiscard]] constexpr Instr as_instr() const {
    ASSERT(is_instr());
    return instr;
  }

  [[nodiscard]] constexpr BasicBlock as_bb() const {
    ASSERT(is_bb());
    return bb;
  }

  [[nodiscard]] constexpr ConstantValueR as_constant() const {
    ASSERT(is_constant());
    return const_val;
  }

  [[nodiscard]] constexpr BBArgument as_bb_arg() const {
    ASSERT(is_bb_arg());
    return bb_arg;
  }
};

}  // namespace foptim::fir

template <>
struct ankerl::unordered_dense::hash<foptim::fir::ValueR> {
  using is_avalanching = void;

  [[nodiscard]] auto operator()(const foptim::fir::ValueR &k) const noexcept
      -> uint64_t {
    using namespace foptim::fir;
    switch (k.ty) {
      case foptim::fir::ValueType::InvalidValue:
        return 0;
      case foptim::fir::ValueType::Instr:
        return hash<Instr>()(k.instr);
      case foptim::fir::ValueType::BasicBlock:
        return hash<BasicBlock>()(k.bb);
      case foptim::fir::ValueType::BBArg:
        return hash<BBArgument>()(k.bb_arg);
      case foptim::fir::ValueType::ConstantValueR:
        return hash<ConstantValueR>()(k.const_val);
    }
  }
};

template <>
struct std::hash<foptim::fir::ValueR> {
  std::size_t operator()(const foptim::fir::ValueR &k) const {
    using foptim::u32;
    using namespace foptim::fir;
    using std::hash;
    switch (k.ty) {
      case foptim::fir::ValueType::InvalidValue:
        return 0;
      case foptim::fir::ValueType::Instr:
        return std::hash<Instr>()(k.instr);
      case foptim::fir::ValueType::BasicBlock:
        return std::hash<BasicBlock>()(k.bb);
      case foptim::fir::ValueType::BBArg:
        return std::hash<BBArgument>()(k.bb_arg);
      case foptim::fir::ValueType::ConstantValueR:
        return std::hash<ConstantValueR>()(k.const_val);
    }
  }
};
