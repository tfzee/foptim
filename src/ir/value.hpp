#pragma once
#include <ankerl/unordered_dense.h>

#include <cassert>

#include "basic_block_ref.hpp"
#include "instruction.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/constant_value.hpp"
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
  friend class Instr;
  constexpr ValueR(Instr v, u8 idx)
      : kind(ValueType::Instr), idx(idx), instr(v) {}

 public:
  ValueType kind;
  // which result of the instruction, always 0 for the other kinds
  u8 idx = 0;
  union {
    Instr instr;
    BasicBlock bb;
    BBArgument bb_arg;
    ConstantValueR const_val;
  };

  constexpr ValueR(const ValueR &) noexcept = default;
  constexpr ValueR(ValueR &&) noexcept = default;
  constexpr ValueR &operator=(const ValueR &) noexcept = default;
  constexpr ValueR &operator=(ValueR &&) noexcept = default;

  void add_usage(Use u);
  [[nodiscard]] size_t get_n_uses() const;
  void remove_usage(Use u, bool verify = true);
  void replace_all_uses(ValueR);
  void replace_uses_outside_block(fir::BasicBlock bb, ValueR new_value);
  [[nodiscard]] IRVec<Use> *get_uses();
  [[nodiscard]] const IRVec<Use> *get_uses() const;
  [[nodiscard]] TypeR get_type() const;

  consteval ValueR() : kind(ValueType::InvalidValue) {}
  explicit constexpr ValueR(ConstantValueR v)
      : kind(ValueType::ConstantValueR), const_val(v) {}
  explicit constexpr ValueR(BasicBlock v)
      : kind(ValueType::BasicBlock), bb(v) {}
  explicit constexpr ValueR(BBArgument v) : kind(ValueType::BBArg), bb_arg(v) {}

  [[nodiscard]] bool eql(const ValueR &other) const;
  // result index of an instruction result
  [[nodiscard]] constexpr u8 result_idx() const {
    ASSERT(is_instr());
    return idx;
  }

  [[nodiscard]] bool operator==(const ValueR &other) const {
    return this->eql(other);
  }
  [[nodiscard]] constexpr bool is_constant() const {
    return kind == ValueType::ConstantValueR;
  }
  [[nodiscard]] constexpr bool is_constant_func() const {
    return kind == ValueType::ConstantValueR && const_val->is_func();
  }
  [[nodiscard]] constexpr bool is_constant_global() const {
    return kind == ValueType::ConstantValueR && const_val->is_global();
  }
  [[nodiscard]] constexpr bool is_const_int(i128 v) const {
    return kind == ValueType::ConstantValueR && const_val->is_int() &&
           const_val->as_int() == v;
  }
  [[nodiscard]] constexpr bool is_constant_int() const {
    return kind == ValueType::ConstantValueR && const_val->is_int();
  }
  [[nodiscard]] constexpr bool is_constant_null() const {
    return kind == ValueType::ConstantValueR && const_val->is_null();
  }
  [[nodiscard]] constexpr bool is_poison() const {
    return kind == ValueType::ConstantValueR && const_val->is_poison();
  }
  [[nodiscard]] constexpr bool is_constant_float() const {
    return kind == ValueType::ConstantValueR && const_val->is_float();
  }
  [[nodiscard]] constexpr bool is_instr() const {
    return kind == ValueType::Instr;
  }
  [[nodiscard]] bool is_instr(fir::InstrType exp) const;
  [[nodiscard]] bool is_instr(fir::BinaryInstrSubType sub_ty) const;
  [[nodiscard]] constexpr bool is_bb() const {
    return kind == ValueType::BasicBlock;
  }
  [[nodiscard]] constexpr bool is_bb_arg() const {
    return kind == ValueType::BBArg;
  }
  [[nodiscard]] constexpr bool is_invalid() const {
    return kind == ValueType::InvalidValue;
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

static_assert(sizeof(ValueR) == 16, "the result index must fit the padding");

inline ValueR Instr::result() const {
  ASSERT(n_results() <= 1);
  return ValueR(*this, 0);
}

inline ValueR Instr::result(u8 idx) const {
  ASSERT(idx < n_results());
  return ValueR(*this, idx);
}

// maps every result of from to the same result of to
template <class Map> void map_results(Instr from, Instr to, Map &map) {
  if (!from.is_multi_result()) {
    map.insert({from.result(), to.result()});
    return;
  }
  for (u8 i = 0; i < from.n_results(); i++) {
    map.insert({from.result(i), to.result(i)});
  }
}
} // namespace foptim::fir

template <> struct ankerl::unordered_dense::hash<foptim::fir::ValueR> {
  using is_avalanching = void;

  [[nodiscard]] auto operator()(const foptim::fir::ValueR &k) const noexcept
      -> uint64_t {
    switch (k.kind) {
      case foptim::fir::ValueType::InvalidValue:
        return 0;
      case foptim::fir::ValueType::Instr:
        return hash<foptim::fir::Instr>()(k.instr) + k.idx;
      case foptim::fir::ValueType::BasicBlock:
        return hash<foptim::fir::BasicBlock>()(k.bb);
      case foptim::fir::ValueType::BBArg:
        return hash<foptim::fir::BBArgument>()(k.bb_arg);
      case foptim::fir::ValueType::ConstantValueR:
        return hash<foptim::fir::ConstantValueR>()(k.const_val);
    }
  }
};

template <> struct std::hash<foptim::fir::ValueR> {
  std::size_t operator()(const foptim::fir::ValueR &k) const {
    using foptim::u32;
    using std::hash;
    switch (k.kind) {
      case foptim::fir::ValueType::InvalidValue:
        return 0;
      case foptim::fir::ValueType::Instr:
        return std::hash<foptim::fir::Instr>()(k.instr) + k.idx;
      case foptim::fir::ValueType::BasicBlock:
        return std::hash<foptim::fir::BasicBlock>()(k.bb);
      case foptim::fir::ValueType::BBArg:
        return std::hash<foptim::fir::BBArgument>()(k.bb_arg);
      case foptim::fir::ValueType::ConstantValueR:
        return std::hash<foptim::fir::ConstantValueR>()(k.const_val);
    }
  }
};
