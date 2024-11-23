#pragma once
#include "../function_pass.hpp"
#include "ir/instruction_data.hpp"
#include "optim/helper/inline.hpp"

namespace foptim::optim {


class BaseInlineAdvisor{
public:
  bool should_be_inlined(const fir::Instr instr){
    bool all_args_are_constant = true;
    for(auto arg: instr->args){
      if(!arg.is_constant()){
        all_args_are_constant = false;
      }
    }
    return all_args_are_constant;
  }
};


template <typename T>
concept InlineAdvisor = requires(T v, fir::Instr instr)
{
    {v.should_be_inlined(instr)} -> std::same_as<bool>;
};


template<InlineAdvisor Advisor = BaseInlineAdvisor>
class Inline final : public FunctionPass {
public:
  void apply(fir::Context & /*unused*/, fir::Function &func) override {
    Advisor adv;

    for (size_t bb_id = 0; bb_id < func.n_bbs(); bb_id++) {
      auto bb = func.basic_blocks[bb_id];
      for (size_t instr_id = 0; instr_id < bb->n_instrs(); instr_id++) {
        if (bb->instructions[instr_id]->is(fir::InstrType::DirectCallInstr) &&
            adv.should_be_inlined(bb->instructions[instr_id])) {
          inline_call(bb->instructions[instr_id]);
          break;
        }
      }
    }
  }
};

} // namespace foptim::optim
