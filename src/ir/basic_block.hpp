#pragma once
#include "function_ref.hpp"
#include "instruction.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/types_ref.hpp"
#include "ir/use.hpp"
#include "utils/vec.hpp"

namespace foptim::fir {

class BasicBlockData : public Used {
public:
  FunctionR func;
  IRVec<Instr> instructions;
  IRVec<BBArgument> args;

  BasicBlockData(FunctionR func) : func(func) {}

  auto &get_instrs() { return instructions; }
  [[nodiscard]] const auto &get_instrs() const { return instructions; }

  size_t get_arg_id(BBArgument searching_arg) {
    for (size_t arg_id = 0; arg_id < args.size(); arg_id++) {
      if (args[arg_id] == searching_arg) {
        return arg_id;
      }
    }
    TODO("Tried to get argid of bbargument thats not an argument of this bb");
  }

  void clear_args();
  void remove_arg(size_t indx);
  void remove_from_parent(bool remove_references, bool cleanup_instr = true,
                          bool delete_instr = false);

  void remove_instr(size_t indx, bool delete_instr);
  static TypeR get_type() { return TypeR(TypeR::invalid()); }

  [[nodiscard]] size_t n_instrs() const { return instructions.size(); }
  [[nodiscard]] size_t n_args() const { return args.size(); }
  bool verify(const Function *) const;

  [[nodiscard]] FunctionR get_parent() const { return func; }
  [[nodiscard]] Instr get_terminator() const {
    if (instructions.empty()) {
      return Instr(Instr::invalid());
    }
    return instructions.back();
  }

  // // NOTE: prob shouldnt use this since it doesnt update shit
  // void set_terminator(Instr newTerm) {
  //   if (instructions.empty()) {
  //     instructions.push_back(newTerm);
  //   } else {
  //     instructions.back() = newTerm;
  //   }
  // }

  auto &get_args() { return args; }
};

} // namespace foptim::fir
