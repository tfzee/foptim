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

  [[nodiscard]] TypeR get_type() const;
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
  using Ty = std::variant<Instr, BasicBlock, BBArgumentR, ConstantValueR,
                          InvalidValue>;
  Ty origin;

  void add_usage(Use u);
  [[nodiscard]] size_t get_n_uses() const;
  void remove_usage(Use u, bool verify = true);
  void replace_all_uses(ValueR);
  [[nodiscard]] IRVec<Use> *get_uses();
  [[nodiscard]] const IRVec<Use> *get_uses() const;
  [[nodiscard]] TypeR get_type() const;

  ValueR() : origin(InvalidValue{}) {}
  explicit ValueR(ConstantValueR v) : origin(v) {}
  explicit ValueR(Instr v) : origin(v) {}
  explicit ValueR(BasicBlock v) : origin(v) {}
  explicit ValueR(BasicBlock v, u32 arg) : origin(BBArgumentR(v, arg)) {}

  [[nodiscard]] bool eql(const ValueR &other) const;

  bool operator==(const ValueR &other) const { return this->eql(other); }

  [[nodiscard]] bool is_constant() const {
    return std::holds_alternative<ConstantValueR>(origin);
  }
  [[nodiscard]] bool is_instr() const {
    return std::holds_alternative<Instr>(origin);
  }
  [[nodiscard]] bool is_bb() const {
    return std::holds_alternative<BasicBlock>(origin);
  }
  [[nodiscard]] bool is_bb_arg() const {
    return std::holds_alternative<BBArgumentR>(origin);
  }

  [[nodiscard]] bool is_valid(bool check_refs) const;

  [[nodiscard]] const Instr as_instr() const {
    if (const auto *res = std::get_if<Instr>(&origin)) {
      return *res;
    }
    std::abort();
  }

  [[nodiscard]] const BasicBlock as_bb() const {
    if (const auto *res = std::get_if<BasicBlock>(&origin)) {
      return *res;
    }
    std::abort();
  }

  [[nodiscard]] const ConstantValueR as_constant() const {
    if (const auto *res = std::get_if<ConstantValueR>(&origin)) {
      return *res;
    }
    std::abort();
  }

  [[nodiscard]] const BBArgumentR as_bb_arg() const {
    if (const auto *res = std::get_if<BBArgumentR>(&origin)) {
      return *res;
    }
    std::abort();
  }

  Instr as_instr() {
    if (auto *res = std::get_if<Instr>(&origin)) {
      return *res;
    }
    std::abort();
  }

  [[nodiscard]] const Ty &get_raw() const { return origin; }
};

} // namespace foptim::fir

template <> struct std::hash<foptim::fir::InvalidValue> {
  std::size_t operator()(const foptim::fir::InvalidValue & /*unused*/) const {
    return 0;
  }
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
