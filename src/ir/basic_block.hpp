#pragma once
#include "function_ref.hpp"
#include "instruction.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/types_ref.hpp"
#include "ir/use.hpp"
#include "utils/logging.hpp"
#include "utils/todo.hpp"
#include "utils/vec.hpp"

namespace foptim::fir {

class BasicBlockData : public Used {
public:
  struct Arg : Used {
    TypeR type;
    Arg(TypeR ty) : Used(), type(ty) {}
  };

  FunctionR func;
  IRVec<Instr> instructions;
  IRVec<Arg> args;

  BasicBlockData(FunctionR func) : func(func) {}

  auto &get_instrs() { return instructions; }
  [[nodiscard]] const auto &get_instrs() const { return instructions; }

  void remove_from_parent(bool remove_references, bool cleanup_instr = true);

  void remove_instr(size_t indx);
  static TypeR get_type() { return TypeR(TypeR::invalid()); }

  [[nodiscard]] size_t n_instrs() const { return instructions.size(); }
  [[nodiscard]] size_t n_args() const { return args.size(); }
  bool verify(const Function *, utils::Printer) const;

  FunctionR get_parent() { return func; }
  [[nodiscard]] const FunctionR get_parent() const { return func; }

  [[nodiscard]] const Instr get_terminator() const {
    if (instructions.empty()) {
      ASSERT(false);
      return Instr(Instr::invalid());
    }
    return instructions.back();
  }

  Instr get_terminator() {
    if (instructions.empty()) {
      return Instr(Instr::invalid());
    }
    return instructions.back();
  }

  void set_terminator(Instr newTerm) {
    if (instructions.empty()) {
      instructions.push_back(newTerm);
    } else {
      instructions.back() = newTerm;
    }
  }

  auto &get_args() { return args; }
};

} // namespace foptim::fir
