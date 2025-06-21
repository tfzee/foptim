#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::optim {

// using BBData = TVec<BitSet>;

class LVN final : public FunctionPass {
public:
  static bool applicable(fir::Instr instr) {
    return !instr->is(fir::InstrType::LoadInstr) &&
           !instr->is(fir::InstrType::StoreInstr) &&
           !instr->is(fir::InstrType::AllocaInstr) &&
           !instr->is(fir::InstrType::CallInstr);
  }

  void apply_lvn(fir::BasicBlock bb) {
    for (size_t i = 0; i < bb->instructions.size(); i++) {
      auto instr = bb->instructions[i];
      for (size_t i2 = i + 1; i2 < bb->instructions.size(); i2++) {
        auto instr2 = bb->instructions[i2];
        (void)instr;
        (void)instr2;
        if (applicable(instr2) && instr->eql_expr(*instr2.get_raw_ptr())) {
          instr2->replace_all_uses(fir::ValueR{instr});
          ASSERT(instr2->get_n_uses() == 0);
          instr2.destroy();
          i2--;
          continue;
        }

        // if we store and afterwards load from teh same address
        //  and there is no other store that could interfere inbetween we can
        //  replace the load(TOOD: unless its volatile)
        if (instr->is(fir::InstrType::StoreInstr) &&
            instr2->is(fir::InstrType::LoadInstr) &&
            instr->get_arg(0) == instr2->get_arg(0) &&
            instr->get_type() == instr2.get_type()) {
          bool pot_store_between = false;
          for (size_t between_i = i + 1; between_i < i2; between_i++) {
            auto binstr = bb->instructions[between_i];
            if (binstr->pot_modifies_mem()) {
              pot_store_between = true;
              break;
            }
          }
          if (!pot_store_between) {
            // fmt::println("No Store between {} {}", instr, instr2);
            instr2->replace_all_uses(instr->get_arg(1));
            continue;
          }
        }

        // if we store and afterwards store to the same address
        //  and there is nobody loading that memory inbetween we can
        //  delete the first store(TOOD: unless its volatile)
        if (instr->is(fir::InstrType::StoreInstr) &&
            instr2->is(fir::InstrType::StoreInstr) &&
            instr->get_arg(0) == instr2->get_arg(0) &&
            instr->get_type()->get_bitwidth() == instr2.get_type()->get_bitwidth()) {
          bool pot_load_between = false;
          for (size_t between_i = i + 1; between_i < i2; between_i++) {
            auto binstr = bb->instructions[between_i];
            if (binstr->pot_reads_mem()) {
              pot_load_between = true;
              break;
            }
          }
          if (!pot_load_between) {
            // fmt::println("No Store between {} {}", instr, instr2);
            instr.destroy();
            i--;
            break;
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
