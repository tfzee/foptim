#pragma once
#include <fmt/std.h>

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

  bool is_pot_store_between(fir::BasicBlock bb, fir::ValueR ptr, u32 size,
                            size_t from, size_t to, AliasAnalyis &aa) {
    for (size_t between_i = from; between_i < to; between_i++) {
      auto binstr = bb->instructions[between_i];
      if (binstr->is(fir::InstrType::StoreInstr)) {
        if (aa.alias(binstr->args[0], ptr, binstr->get_type()->get_size(),
                     size) != AliasAnalyis::AAResult::NoAlias) {
          return true;
        }
      } else if (binstr->pot_modifies_mem()) {
        return true;
      }
    }
    return false;
  }

  bool is_pot_load_between(fir::BasicBlock bb, fir::ValueR ptr, u32 size,
                           size_t from, size_t to, AliasAnalyis &aa) {
    for (size_t between_i = from; between_i < to; between_i++) {
      auto binstr = bb->instructions[between_i];
      if (binstr->is(fir::InstrType::LoadInstr)) {
        if (aa.alias(binstr->args[0], ptr, binstr->get_type()->get_size(),
                     size) != AliasAnalyis::AAResult::NoAlias) {
          return true;
        }
      } else if (binstr->pot_reads_mem()) {
        return true;
      }
    }
    return false;
  }

  bool is_pot_loadstore_between(fir::BasicBlock bb, fir::ValueR ptr, u32 size,
                                size_t from, size_t to, AliasAnalyis &aa) {
    for (size_t between_i = from; between_i < to; between_i++) {
      auto binstr = bb->instructions[between_i];
      if (binstr->is(fir::InstrType::StoreInstr) ||
          binstr->is(fir::InstrType::LoadInstr)) {
        if (aa.alias(binstr->args[0], ptr, binstr->get_type()->get_size(),
                     size) != AliasAnalyis::AAResult::NoAlias) {
          return true;
        }
      } else if (binstr->pot_reads_mem() || binstr->pot_modifies_mem()) {
        return true;
      }
    }
    return false;
  }

  bool eql_instr_expr(fir::Instr a, fir::Instr b) {
    if (a->eql_expr(*b.get_raw_ptr()) && a->get_type() == b.get_type()) {
      return true;
    }
    if (a->is(fir::InstrType::BinaryInstr) &&
        b->is(fir::InstrType::BinaryInstr) && a->subtype == b->subtype &&
        a->is_commutative() && a->args[0].eql(b->args[1]) &&
        a->args[1].eql(b->args[0]) &&
        a->get_type()->get_bitwidth() == a->get_type()->get_bitwidth()) {
      return true;
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
        ASSERT(i2 > i);
        auto instr = bb->instructions[i];
        auto instr2 = bb->instructions[i2];
        if (lvn_applicable(instr2)) {
          if (eql_instr_expr(instr, instr2)) {
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
            instr->get_arg(0) == instr2->get_arg(0)) {
          auto t1 = instr->get_type();
          auto t2 = instr2->get_type();

          if (
              // t1->is_vec() && t2->is_vec() &&
              t1->get_bitwidth() == t2->get_bitwidth()) {
            bool pot_store_between = is_pot_store_between(
                bb, instr->args[0], instr->get_type()->get_size(), i + 1, i2,
                aa);
            if (!pot_store_between) {
              if (t1 == t2) {
                instr2->replace_all_uses(instr->get_arg(1));
              } else {
                fir::Builder buh{instr};
                auto r = buh.build_conversion_op(
                    instr->get_arg(1), instr2->get_type(),
                    fir::ConversionSubType::BitCast);
                instr2->replace_all_uses(r);
              }
              instr2.destroy();
              i2--;
              continue;
            }
          }
          // if (t1 == t2) {
          //   bool pot_store_between = is_pot_store_between(
          //       bb, instr->args[0], instr->get_type()->get_size(), i + 1, i2,
          //       aa);
          //   if (!pot_store_between) {
          //     instr2->replace_all_uses(instr->get_arg(1));
          //     instr2.destroy();
          //     i2--;
          //     continue;
          //   }
          // }
        }

        // if we store and afterwards store to the same address
        //  and there is nobody loading that memory inbetween we can
        //  delete the first store(TOOD: unless its volatile)
        if (instr->is(fir::InstrType::StoreInstr) &&
            instr2->is(fir::InstrType::StoreInstr) &&
            instr->get_type()->get_bitwidth() ==
                instr2.get_type()->get_bitwidth() &&
            instr->get_arg(0) == instr2->get_arg(0)) {
          bool pot_load_between = is_pot_load_between(
              bb, instr->args[0], instr->get_type()->get_size(), i + 1, i2, aa);
          if (!pot_load_between) {
            instr.destroy();
            i--;
            break;
          }
        }

        // merge two stores of for example i8 into 1 i16 store
        if (instr->is(fir::InstrType::StoreInstr) &&
            instr2->is(fir::InstrType::StoreInstr) &&
            instr->get_type()->get_bitwidth() ==
                instr2.get_type()->get_bitwidth() &&
            instr->args[0].is_instr() && instr2->args[0].is_instr()) {
          auto arg1 = instr->args[0].as_instr();
          auto arg2 = instr2->args[0].as_instr();
          auto old_width = instr->get_type()->get_size();
          fir::ValueR base1_addr = fir::ValueR{arg1};
          i128 base1_off = 0;
          fir::ValueR base2_addr = fir::ValueR{arg2};
          i128 base2_off = 0;

          if (arg1->is(fir::BinaryInstrSubType::IntAdd) &&
              arg1->args[1].is_constant() &&
              arg1->args[1].as_constant()->is_int()) {
            base1_addr = arg1->args[0];
            base1_off = arg1->args[1].as_constant()->as_int();
          }
          if (arg2->is(fir::BinaryInstrSubType::IntAdd) &&
              arg2->args[1].is_constant() &&
              arg2->args[1].as_constant()->is_int()) {
            base2_addr = arg2->args[0];
            base2_off = arg2->args[1].as_constant()->as_int();
          }
          if (instr->get_type()->is_vec() && old_width <= 16 &&
              base1_addr == base2_addr && base1_off + old_width == base2_off &&
              instr->get_type() == instr2->get_type()) {
            auto v1_type = instr->get_type()->as_vec();
            bool pot_load_between = is_pot_loadstore_between(
                bb, instr->args[0], old_width * 2, i + 1, i2, aa);
            if (!pot_load_between) {
              fir::Builder buh{instr2};
              auto *ctx = bb->get_parent()->ctx;
              auto new_type = ctx->get_vec_type(v1_type.type, v1_type.bitwidth,
                                                v1_type.member_number * 2);
              auto v1 = instr->args[1];
              auto v2 = instr2->args[1];
              auto data = buh.build_vector_op(v1, v2, new_type,
                                              fir::VectorISubType::Concat);
              buh.build_store(instr->args[0], data);
              // fmt::println("===========MERGED=========0");
              // fmt::println("{:cd}", instr);
              // fmt::println("{:cd}", instr2);
              // fmt::println("==");
              // fmt::println("{:cd}", v1_ext.as_instr());
              // fmt::println("{:cd}", v2_ext.as_instr());
              // fmt::println("{:cd}", shift2_val.as_instr());
              // fmt::println("{:cd}", data.as_instr());
              // fmt::println("{:cd}", store.as_instr());
              // fmt::println("{:cd} =SSM> {:cd}", instr, instr2);
              instr.destroy();
              instr2.destroy();
              i--;
              break;
            }
          }
          if (instr->get_type()->is_int() && old_width <= 4 &&
              base1_addr == base2_addr && base1_off + old_width == base2_off) {
            bool pot_load_between = is_pot_loadstore_between(
                bb, instr->args[0], old_width * 2, i + 1, i2, aa);
            if (!pot_load_between) {
              fir::Builder buh{instr2};
              auto *ctx = bb->get_parent()->ctx;
              auto new_type = ctx->get_int_type(2 * old_width * 8);
              auto v1 = instr->args[1];
              auto v2 = instr2->args[1];
              auto v1_ext = buh.build_zext(v1, new_type);
              auto v2_ext = buh.build_zext(v2, new_type);
              auto shift2_val = buh.build_binary_op(
                  v2_ext,
                  fir::ValueR{ctx->get_constant_value(old_width * 8, new_type)},
                  fir::BinaryInstrSubType::Shl);
              auto data = buh.build_binary_op(v1_ext, shift2_val,
                                              fir::BinaryInstrSubType::Or);
              buh.build_store(instr->args[0], data);
              // fmt::println("===========MERGED=========0");
              // fmt::println("{:cd}", instr);
              // fmt::println("{:cd}", instr2);
              // fmt::println("==");
              // fmt::println("{:cd}", v1_ext.as_instr());
              // fmt::println("{:cd}", v2_ext.as_instr());
              // fmt::println("{:cd}", shift2_val.as_instr());
              // fmt::println("{:cd}", data.as_instr());
              // fmt::println("{:cd}", store.as_instr());
              // fmt::println("{:cd} =SSM> {:cd}", instr, instr2);
              instr.destroy();
              instr2.destroy();
              i--;
              break;
            }
          }
        }

        // if we load and afterwards load form the same address
        //  and there is nobody storing that memory inbetween we can
        //  delete the first load(TOOD: unless its volatile)
        if (instr->is(fir::InstrType::LoadInstr) &&
            instr2->is(fir::InstrType::LoadInstr) &&
            instr->get_type()->get_bitwidth() ==
                instr2.get_type()->get_bitwidth() &&
            instr->get_arg(0) == instr2->get_arg(0)) {
          bool pot_store_between = is_pot_store_between(
              bb, instr->args[0], instr->get_type()->get_size(), i + 1, i2, aa);
          if (!pot_store_between) {
            instr2->replace_all_uses(fir::ValueR{instr});
            instr2.destroy();
            i2--;
            continue;
          }
        }

        // merge 2 loads
        if (instr->is(fir::InstrType::LoadInstr) &&
            instr2->is(fir::InstrType::LoadInstr) &&
            instr->get_type()->get_bitwidth() ==
                instr2.get_type()->get_bitwidth() &&
            instr->args[0].is_instr() && instr2->args[0].is_instr()) {
          auto arg1 = instr->args[0].as_instr();
          auto arg2 = instr2->args[0].as_instr();
          auto old_width = instr->get_type()->get_size();
          fir::ValueR base1_addr = fir::ValueR{arg1};
          i128 base1_off = 0;
          fir::ValueR base2_addr = fir::ValueR{arg2};
          i128 base2_off = 0;

          if (arg1->is(fir::BinaryInstrSubType::IntAdd) &&
              arg1->args[1].is_constant() &&
              arg1->args[1].as_constant()->is_int()) {
            base1_addr = arg1->args[0];
            base1_off = arg1->args[1].as_constant()->as_int();
          }
          if (arg2->is(fir::BinaryInstrSubType::IntAdd) &&
              arg2->args[1].is_constant() &&
              arg2->args[1].as_constant()->is_int()) {
            base2_addr = arg2->args[0];
            base2_off = arg2->args[1].as_constant()->as_int();
          }
          if (instr->get_type()->is_vec() && old_width <= 16 &&
              base1_addr == base2_addr && base1_off + old_width == base2_off &&
              instr->get_type() == instr2->get_type()) {
            auto v1_type = instr->get_type()->as_vec();
            bool pot_store_between = is_pot_store_between(
                bb, instr->args[0], old_width * 2, i + 1, i2, aa);
            if (!pot_store_between) {
              fir::Builder buh{instr};
              auto *ctx = bb->get_parent()->ctx;
              auto new_type = ctx->get_vec_type(v1_type.type, v1_type.bitwidth,
                                                v1_type.member_number * 2);
              auto loaded_data = buh.build_load(new_type, fir::ValueR{arg1});
              auto data1 = buh.build_vector_op(loaded_data, instr->get_type(),
                                               fir::VectorISubType::ExtractLow);
              auto data2 =
                  buh.build_vector_op(loaded_data, instr->get_type(),
                                      fir::VectorISubType::ExtractHigh);
              instr->replace_all_uses(data1);
              instr2->replace_all_uses(data2);
              instr.destroy();
              instr2.destroy();
              i--;
              break;
            }
          }
          if (instr->get_type()->is_int() && old_width <= 4 &&
              base1_addr == base2_addr && base1_off + old_width == base2_off) {
            bool pot_store_between = is_pot_store_between(
                bb, instr->args[0], old_width * 2, i + 1, i2, aa);
            if (!pot_store_between) {
              // fmt::println("{:cd}", bb);
              fir::Builder buh{instr};
              auto *ctx = bb->get_parent()->ctx;
              auto new_type = ctx->get_int_type(2 * old_width * 8);
              auto loaded_data = buh.build_load(new_type, fir::ValueR{arg1});
              auto v1_val = buh.build_itrunc(loaded_data, instr->get_type());
              auto shift2_val = buh.build_binary_op(
                  loaded_data,
                  fir::ValueR{ctx->get_constant_value(old_width * 8, new_type)},
                  fir::BinaryInstrSubType::Shr);
              auto v2_val = buh.build_itrunc(shift2_val, instr->get_type());
              instr->replace_all_uses(v1_val);
              instr2->replace_all_uses(v2_val);
              instr.destroy();
              instr2.destroy();
              // fmt::println("{:cd}", bb);
              // fmt::println("==========================");
              // TODO("merge load==");
              i--;
              break;
            }
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
}  // namespace foptim::optim
