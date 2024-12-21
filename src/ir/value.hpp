#pragma once
#include "basic_block_ref.hpp"
#include "instruction.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/constant_value.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/use.hpp"
#include "types_ref.hpp"
#include "utils/logging.hpp"
#include <cassert>
#include <variant>

namespace foptim::fir {

struct InvalidValue {};

class ValueR {
public:
  using Ty =
      std::variant<Instr, BasicBlock, BBArgument, ConstantValueR, InvalidValue>;
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
  explicit ValueR(BBArgument v) : origin(v) {}

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
    return std::holds_alternative<BBArgument>(origin);
  }

  [[nodiscard]] bool is_valid(bool check_refs) const;

  [[nodiscard]] Instr as_instr() const {
    if (const auto *res = std::get_if<Instr>(&origin)) {
      return *res;
    }
    std::abort();
  }

  [[nodiscard]] BasicBlock as_bb() const {
    if (const auto *res = std::get_if<BasicBlock>(&origin)) {
      return *res;
    }
    std::abort();
  }

  [[nodiscard]] ConstantValueR as_constant() const {
    if (const auto *res = std::get_if<ConstantValueR>(&origin)) {
      return *res;
    }
    std::abort();
  }

  [[nodiscard]] BBArgument as_bb_arg() const {
    if (const auto *res = std::get_if<BBArgument>(&origin)) {
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
