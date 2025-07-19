#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"

namespace foptim::optim {

// using BBData = TVec<BitSet>;

class LVN final : public FunctionPass {
public:
  static bool lvn_applicable(fir::Instr instr) {
    return !instr->is(fir::InstrType::LoadInstr) &&
           !instr->is(fir::InstrType::StoreInstr) &&
           !instr->is(fir::InstrType::AllocaInstr) &&
           !instr->is(fir::InstrType::CallInstr);
  }
  static bool gvn_applicable(fir::Instr instr) {
    return !instr->is(fir::InstrType::LoadInstr) &&
           !instr->is(fir::InstrType::StoreInstr) &&
           !instr->is(fir::InstrType::AllocaInstr) &&
           !instr->is(fir::InstrType::CallInstr) &&
           !instr->has_pot_sideeffects() && !instr->is_critical();
  }

  bool apply_gvn(fir::Instr instr, fir::BasicBlock bb, const CFG &cfg,
                 const Dominators &dom) {
    auto bb_id = cfg.get_bb_id(bb);
    for (auto d : dom.dom_bbs[bb_id].dominators) {
      if (d == bb_id) {
        // lvn handles this case
        continue;
      }
      auto prev_bb = cfg.bbrs[d].bb;
      for (auto instr2 : prev_bb->instructions) {
        if (instr->eql_expr(*instr2.get_raw_ptr()) &&
            instr->get_type() == instr2.get_type()) {
          instr->replace_all_uses(fir::ValueR{instr2});
          instr.destroy();
          return true;
        }
      }
    }
    return false;
  }

  void apply_lvn(fir::BasicBlock bb, const CFG &cfg, const Dominators &dom,
                 AliasAnalyis &aa) {
    for (size_t i = 0; i < bb->instructions.size(); i++) {
      auto instr = bb->instructions[i];
      if (gvn_applicable(instr) && apply_gvn(instr, bb, cfg, dom)) {
        i--;
        continue;
      }

      for (size_t i2 = i + 1; i2 < bb->instructions.size(); i2++) {
        auto instr2 = bb->instructions[i2];
        if (lvn_applicable(instr2) && instr->get_type() == instr2.get_type()) {
          if (instr->eql_expr(*instr2.get_raw_ptr())) {
            instr2->replace_all_uses(fir::ValueR{instr});
            ASSERT(instr2->get_n_uses() == 0);
            instr2.destroy();
            i2--;
            continue;
          }
          if (instr->is(fir::InstrType::ICmp) &&
              instr2->is(fir::InstrType::ICmp) &&
              instr->args[0].eql(instr2->args[0]) &&
              instr->args[1].eql(instr2->args[1])) {
            bool opposite_eql =
                (instr->subtype == (u32)fir::ICmpInstrSubType::EQ &&
                 instr2->subtype == (u32)fir::ICmpInstrSubType::NE) ||
                (instr->subtype == (u32)fir::ICmpInstrSubType::NE &&
                 instr2->subtype == (u32)fir::ICmpInstrSubType::EQ);
            if (opposite_eql) {
              fir::Builder bb{instr2};
              auto res = bb.build_unary_op(fir::ValueR{instr},
                                           fir::UnaryInstrSubType::Not);
              instr2->replace_all_uses(res);
              instr2.destroy();
              i2--;
              continue;
            }
          }
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
            if (binstr->is(fir::InstrType::StoreInstr)) {
              if (aa.alias(binstr->args[0], instr->args[0]) !=
                  AliasAnalyis::AAResult::NoAlias) {
                pot_store_between = true;
                break;
              }
            } else if (binstr->pot_modifies_mem()) {
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
            instr->get_type()->get_bitwidth() ==
                instr2.get_type()->get_bitwidth()) {
          bool pot_load_between = false;
          for (size_t between_i = i + 1; between_i < i2; between_i++) {
            auto binstr = bb->instructions[between_i];
            if (binstr->is(fir::InstrType::LoadInstr)) {
              if (aa.alias(binstr->args[0], instr->args[0]) !=
                  AliasAnalyis::AAResult::NoAlias) {
                pot_load_between = true;
                break;
              }
            } else if (binstr->pot_reads_mem()) {
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
    ZoneScopedNC("LVN", COLOR_OPTIMF);
    CFG cfg{func};
    Dominators dom{cfg};
    AliasAnalyis aa;
    for (auto bb : func.basic_blocks) {
      apply_lvn(bb, cfg, dom, aa);
    }
  }
};
} // namespace foptim::optim
