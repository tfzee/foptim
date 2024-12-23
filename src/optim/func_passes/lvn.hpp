#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "utils/logging.hpp"

namespace foptim::optim {

// using BBData = TVec<BitSet>;

class LVN final : public FunctionPass {
public:
  static bool applicable(fir::Instr instr) {
    return !instr->is(fir::InstrType::LoadInstr) &&
           !instr->is(fir::InstrType::AllocaInstr) &&
           !instr->is(fir::InstrType::CallInstr);
  }

  void apply_lvn(fir::BasicBlock bb) {
    for (size_t i = 0; i < bb->instructions.size(); i++) {
      const auto instr = bb->instructions[i];
      for (size_t i2 = i + 1; i2 < bb->instructions.size(); i2++) {
        auto instr2 = bb->instructions[i2];
        if (applicable(instr2) && instr->eql_expr(*instr2.get_raw_ptr())) {
          instr2->replace_all_uses(fir::ValueR{instr});
        }

        // if we store and afterwards load from teh same address
        //  and there is no other store that could interfere inbetween we can
        //  replace the load
        if (instr->is(fir::InstrType::StoreInstr) &&
            instr2->is(fir::InstrType::LoadInstr) &&
            instr->get_arg(0) == instr2->get_arg(0)) {
          bool pot_store_between = false;
          for (size_t between_i = i + 1; between_i < i2; between_i++) {
            auto binstr = bb->instructions[between_i];
            if (binstr->pot_modifies_mem()) {
              pot_store_between = true;
              break;
            }
          }
          if (!pot_store_between) {
            instr2->replace_all_uses(instr->get_arg(1));
            // utils::Debug << "maybe optimize" << instr << " " << instr2 <<
            // "\n";
          } else {
            failure(
                {"StoreLoadElim Store inbetween ", {bb}});
          }
        }
      }
    }
  }

  void apply(fir::Context & /*unused*/, fir::Function &func) override {
    // ZoneScopedN("GVN TODO");
    // {
    ZoneScopedN("LVN");
    for (auto bb : func.basic_blocks) {
      apply_lvn(bb);
    }
    // }
  }
};
} // namespace foptim::optim
