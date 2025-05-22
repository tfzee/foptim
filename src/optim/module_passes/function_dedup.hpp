#pragma once
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "optim/module_pass.hpp"
#include <fmt/core.h>

namespace foptim::optim {

struct DiffConst {
  fir::Use use1;
  fir::Use use2;
};

[[nodiscard]] inline bool
check_args2(fir::Instr i1, fir::Instr i2,
            TMap<fir::ValueR, fir::ValueR> &local_value_map,
            TVec<DiffConst> &difference_values) {
  for (u32 i = 0; i < i1->args.size(); i++) {
    auto &arg1 = i1->args[i];
    auto &arg2 = i2->args[i];
    if (arg1 == arg2) {
      continue;
    }
    // if either is a local arg
    //  then both need to be and both need topoint to the same
    if (local_value_map.contains(arg1)) {
      if (local_value_map.at(arg1) == arg2) {
        continue;
      }
      return false;
    }
    if (local_value_map.contains(arg2)) {
      return false;
    }
    // else if they aren teh same + they are the same local we cancel
    if (!arg1.is_constant() || !arg2.is_constant() ||
        i1->is(fir::InstrType::AllocaInstr)) {
      return false;
    }
    difference_values.push_back({fir::Use::norm(i1, i), fir::Use::norm(i2, i)});
  }
  return true;
}

[[nodiscard]] inline bool
match_term2(fir::Instr i1, fir::Instr i2,
            TMap<fir::ValueR, fir::ValueR> &local_value_map,
            TVec<DiffConst> &difference_values) {
  if (i1 == i2) {
    return true;
  }
  if (i1->instr_type != i2->instr_type || i1->subtype != i2->subtype ||
      i1->args.size() != i2->args.size()) {
    return false;
  }
  for (size_t bb_id = 0; bb_id < i1->bbs.size(); bb_id++) {
    auto &bb1 = i1->bbs[bb_id];
    auto &bb2 = i2->bbs[bb_id];
    if (local_value_map.at(fir::ValueR(bb1.bb)).as_bb() != bb2.bb) {
      return false;
    }
    if (bb1.args.size() != bb2.args.size()) {
      return false;
    }
    for (size_t arg_id = 0; arg_id < bb1.args.size(); arg_id++) {
      auto &a1 = bb1.args[arg_id];
      auto &a2 = bb2.args[arg_id];
      if (a1 == a2) {
        continue;
      }
      if (local_value_map.contains(a1)) {
        if (local_value_map.at(a1) == a2) {
          continue;
        }
        return false;
      }
      if (local_value_map.contains(a2)) {
        return false;
      }

      if (!a1.is_constant() || !a2.is_constant()) {
        return false;
      }
      difference_values.push_back({fir::Use::bb_arg(i1, bb_id, arg_id),
                                   fir::Use::bb_arg(i2, bb_id, arg_id)});
    }
  }
  return check_args2(i1, i2, local_value_map, difference_values);
}

class FunctionDeDup final : public ModulePass {
public:
  void apply(fir::Context &ctx) override {
    ZoneScopedN("FunctionDeDup");
    (void)ctx;

    for (auto &e1 : ctx.data->storage.functions) {
      auto &f1 = e1.second;
      if (f1->is_decl()) {
        continue;
      }
      auto f1_ninstrs = f1->n_instrs();

      for (auto &e2 : ctx.data->storage.functions) {
        auto &f2 = e2.second;
        if (f1->name == f2->name || f2->is_decl()) {
          continue;
        }
        // could be overwritten later so we cant rely on the function body
        if (f1->linkage == fir::Function::Linkage::Weak ||
            f1->linkage == fir::Function::Linkage::WeakODR ||
            f1->linkage == fir::Function::Linkage::LinkOnce ||
            f2->linkage == fir::Function::Linkage::Weak ||
            f2->linkage == fir::Function::Linkage::WeakODR ||
            f2->linkage == fir::Function::Linkage::LinkOnce) {
          continue;
        }

        if (f1->basic_blocks.size() != f2->basic_blocks.size() ||
            f1->get_entry()->n_args() != f2->get_entry()->n_args() ||
            f1_ninstrs != f2->n_instrs()) {
          continue;
        }

        TMap<fir::ValueR, fir::ValueR> local_value_map;
        TVec<DiffConst> difference_values;

        bool successful = true;
        // fill up the local value map
        for (size_t bb_id = 0; bb_id < f1->basic_blocks.size(); bb_id++) {
          auto bb1 = f1->basic_blocks[bb_id];
          auto bb2 = f2->basic_blocks[bb_id];
          local_value_map.insert({fir::ValueR(bb1), fir::ValueR(bb2)});
          local_value_map.insert({fir::ValueR(bb2), fir::ValueR(bb1)});
          if (bb1->n_instrs() != bb2->n_instrs() ||
              bb1->n_args() != bb2->n_args()) {
            successful = false;
            break;
          }
          for (size_t i = 0; i < bb1->n_args(); i++) {
            auto i1 = bb1->args[i];
            auto i2 = bb2->args[i];
            local_value_map.insert({fir::ValueR(i1), fir::ValueR(i2)});
            local_value_map.insert({fir::ValueR(i2), fir::ValueR(i1)});
          }
          for (size_t i = 0; i < f1->basic_blocks[bb_id]->instructions.size();
               i++) {
            auto i1 = bb1->instructions[i];
            auto i2 = bb2->instructions[i];
            local_value_map.insert({fir::ValueR(i1), fir::ValueR(i2)});
            local_value_map.insert({fir::ValueR(i2), fir::ValueR(i1)});
          }
        }
        if (!successful) {
          continue;
        }

        // then check if each instruction matches
        for (size_t bb_id = 0; bb_id < f1->basic_blocks.size(); bb_id++) {
          auto bb1 = f1->basic_blocks[bb_id];
          auto bb2 = f2->basic_blocks[bb_id];

          if (!match_term2(bb1->get_terminator(), bb2->get_terminator(),
                           local_value_map, difference_values)) {
            successful = false;
            break;
          }
          for (size_t i = 0; i < bb1->instructions.size(); i++) {
            auto i1 = bb1->instructions[i];
            auto i2 = bb2->instructions[i];
            if (i1 == i2) {
              continue;
            }
            if (i1->instr_type != i2->instr_type ||
                i1->subtype != i2->subtype ||
                i1->args.size() != i2->args.size()) {
              successful = false;
              break;
            }

            if (!check_args2(i1, i2, local_value_map, difference_values)) {
              successful = false;
              break;
            }
          }
          if (!successful) {
            break;
          }
        }
        if (!successful) {
          continue;
        }

        // TODO: heuristic
        if (difference_values.size() > f1_ninstrs ||
            difference_values.size() > 5) {
          continue;
        }

        // if theres no differnces just forward all calls to the one with more
        // references
        if (difference_values.empty()) {
          //  TOOD: prob better way to handle this
          fir::Function *target = f1.get();
          fir::Function *looser = f2.get();
          if (f1->linkage == fir::Function::Linkage::Weak ||
              f1->linkage == fir::Function::Linkage::WeakODR ||
              f1->linkage == fir::Function::Linkage::LinkOnce) {
            target = f2.get();
            looser = f1.get();
          }
          (void)target;
          (void)looser;
          // fmt::println("MERGED {} {}", *f1, *f2);
          TVec<fir::Use> uses{looser->get_uses().begin(),
                              looser->get_uses().end()};
          for (auto use : uses) {
            use.replace_use(
                fir::ValueR(ctx->get_constant_value(fir::FunctionR{target})));
          }
        } else {
          if ((f1->linkage == fir::Function::Linkage::LinkOnceODR ||
               f1->linkage == fir::Function::Linkage::Internal) ||
              (f2->linkage == fir::Function::Linkage::LinkOnceODR ||
               f2->linkage == fir::Function::Linkage::Internal)) {
            // fmt::println("{}", difference_values.size());
            // TODO("impl constant merging");
          }
        }
      }
    }
  }
};
} // namespace foptim::optim
