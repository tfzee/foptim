#pragma once
#include "../function_pass.hpp"
#include "ir/instruction_data.hpp"
#include "optim/helper/inline.hpp"

namespace foptim::optim {

class BaseInlineAdvisor {
public:
  bool should_be_inlined(const fir::Instr instr) {
    bool all_args_are_constant = true;
    for (auto arg : instr->args) {
      if (!arg.is_constant()) {
        all_args_are_constant = false;
      }
    }
    auto func = instr->get_parent()->get_parent();
    auto called_func = instr->get_arg(0);
    if (called_func.is_constant() && called_func.as_constant()->is_func()) {
      auto v = called_func.as_constant()->as_func();
      if (v.func->get_n_uses() == 1 &&
          v.func->linkage == fir::Function::Function::Linkage::Internal) {
        return true;
      }
      if (v.func == func.func) {
        return true;
      }
    }

    return all_args_are_constant;
  }
};

template <typename T>
concept InlineAdvisor = requires(T v, fir::Instr instr) {
  { v.should_be_inlined(instr) } -> std::same_as<bool>;
};

template <InlineAdvisor Advisor = BaseInlineAdvisor>
class Inline final : public FunctionPass {
public:
  void apply(fir::Context & /*unused*/, fir::Function &func) override {
    Advisor adv;
    TVec<fir::Instr> calls;

    for (size_t bb_id = 0; bb_id < func.n_bbs(); bb_id++) {
      auto bb = func.basic_blocks[bb_id];
      for (size_t instr_id = 0; instr_id < bb->n_instrs(); instr_id++) {
        if (bb->instructions[instr_id]->is(fir::InstrType::CallInstr) &&
            adv.should_be_inlined(bb->instructions[instr_id])) {
          calls.push_back(bb->instructions[instr_id]);
          break;
        }
      }
    }
    for (auto call : calls) {
      // fmt::println("INLINING {}", call);
      inline_call(call);
    }
  }
};

} // namespace foptim::optim
