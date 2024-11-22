#pragma once
#include "../function_pass.hpp"
#include "ir/instruction_data.hpp"
#include "optim/helper/inline.hpp"

namespace foptim::optim {

class Inline final : public FunctionPass {
public:
  void apply(fir::Context &, fir::Function &func) override {
    for (size_t bb_id = 0; bb_id < func.n_bbs(); bb_id++) {
      auto bb = func.basic_blocks[bb_id];
      for (size_t instr_id = 0; instr_id < bb->n_instrs(); instr_id++) {
        if (bb->instructions[instr_id]->is(fir::InstrType::DirectCallInstr)) {
          // inline_call(bb->instructions[instr_id]);
          break;
        }
      }
    }
  }
};

} // namespace foptim::optim
