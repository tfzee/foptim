#pragma once
#include "basic_block_ref.hpp"
#include "instruction.hpp"
#include "ir/constant_value.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/use.hpp"
#include "types_ref.hpp"
#include "utils/logging.hpp"
#include <cassert>
#include <variant>

namespace foptim::fir {

class BBArgumentR {
public:
  BasicBlock bb;
  u32 arg;

  BBArgumentR(BasicBlock bb, u32 arg) : bb(bb), arg(arg) {}

  bool operator==(const BBArgumentR &other) const {
    return bb == other.bb && arg == other.arg;
  }

  TypeR get_type() const;
};
} // namespace foptim::fir

template <> struct std::hash<foptim::fir::BBArgumentR> {
  std::size_t operator()(const foptim::fir::BBArgumentR &k) const {
    using foptim::u32;
    using std::hash;
    return hash<foptim::fir::BasicBlock>()(k.bb) ^ hash<u32>()(k.arg);
  }
};

namespace foptim::fir {
struct InvalidValue {};

class ValueR {
public:
  using Ty = std::variant<Instr, BBArgumentR, ConstantValueR, InvalidValue>;
  Ty origin;

public:
  void add_usage(Use u);
  size_t get_n_uses() const;
  FVec<Use> *get_uses();
  const FVec<Use> *get_uses() const;
  void remove_usage(Use u);
  void replace_all_uses(ValueR);
  TypeR get_type() const;

public:
  ValueR() : origin(InvalidValue{}) {}
  explicit ValueR(ConstantValueR v) : origin(v) {}
  explicit ValueR(Instr v) : origin(v) {}
  explicit ValueR(BasicBlock v, u32 arg) : origin(BBArgumentR(v, arg)) {}

  bool eql(const ValueR &other) const;

  bool operator==(const ValueR &other) const { return this->eql(other); }

  bool is_constant() const { return std::holds_alternative<ConstantValueR>(origin); }
  bool is_instr() const { return std::holds_alternative<Instr>(origin); }
  bool is_bb_arg() const { return std::holds_alternative<BBArgumentR>(origin); }

  bool is_valid() {
    return !std::holds_alternative<InvalidValue>(origin);
    // TODO: could also check for valid refs
  }

  const Instr as_instr() const {
    if (auto *res = std::get_if<Instr>(&origin)) {
      return *res;
    } else {
      std::abort();
    }
  }

  const ConstantValueR as_constant() const {
    if (auto *res = std::get_if<ConstantValueR>(&origin)) {
      return *res;
    } else {
      std::abort();
    }
  }

  const BBArgumentR as_bb_arg() const {
    if (auto *res = std::get_if<BBArgumentR>(&origin)) {
      return *res;
    } else {
      std::abort();
    }
  }

  Instr as_instr() {
    if (auto *res = std::get_if<Instr>(&origin)) {
      return *res;
    } else {
      std::abort();
    }
  }

  const Ty &get_raw() { return origin; }
};

} // namespace foptim::fir

template <> struct std::hash<foptim::fir::InvalidValue> {
  std::size_t operator()(const foptim::fir::InvalidValue &) const { return 0; }
};

template <> struct std::hash<foptim::fir::ValueR> {
  std::size_t operator()(const foptim::fir::ValueR &k) const {
    using foptim::u32;
    using std::hash;
    return std::visit(
        [](auto &&v) {
          return std::hash<typename std::remove_const<typeof(v)>::type>()(v);
        },
        k.origin);
  }
};
