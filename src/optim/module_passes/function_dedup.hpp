#pragma once
#include "ir/basic_block_arg.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "optim/module_pass.hpp"
#include <fmt/core.h>

namespace foptim::optim {

struct DiffConst {
  fir::Instr i1;
  fir::Instr i2;
  u32 arg_id;
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
    // TODO: could do dynamic alloca bt idk if worth it could use cost variable
    // to keep track of this
    if (!arg1.is_constant() || !arg2.is_constant() ||
        i1->is(fir::InstrType::AllocaInstr) ||
        arg1.get_type() != arg2.get_type()) {
      return false;
    }
    if(i1->is(fir::InstrType::CallInstr) || i == 0){
      return false;
    }
    difference_values.push_back({i1, i2, i});
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

      // if (!a1.is_constant() || !a2.is_constant()) {
      return false;
      // }
      // difference_values.push_back({fir::Use::bb_arg(i1, bb_id, arg_id),
      //                              fir::Use::bb_arg(i2, bb_id, arg_id)});
    }
  }
  return check_args2(i1, i2, local_value_map, difference_values);
}

inline bool check_match(std::unique_ptr<fir::Function> &f1,
                        std::unique_ptr<fir::Function> &f2,
                        TMap<fir::ValueR, fir::ValueR> &local_value_map,
                        TVec<DiffConst> &difference_values) {
  // fill up the local value map
  for (size_t bb_id = 0; bb_id < f1->basic_blocks.size(); bb_id++) {
    auto bb1 = f1->basic_blocks[bb_id];
    auto bb2 = f2->basic_blocks[bb_id];
    local_value_map.insert({fir::ValueR(bb1), fir::ValueR(bb2)});
    local_value_map.insert({fir::ValueR(bb2), fir::ValueR(bb1)});
    if (bb1->n_instrs() != bb2->n_instrs() || bb1->n_args() != bb2->n_args()) {
      return false;
    }
    for (size_t i = 0; i < bb1->n_args(); i++) {
      if (bb1->args[i]->get_type() != bb2->args[i]->get_type()) {
        return false;
      }
      auto i1 = bb1->args[i];
      auto i2 = bb2->args[i];
      local_value_map.insert({fir::ValueR(i1), fir::ValueR(i2)});
      local_value_map.insert({fir::ValueR(i2), fir::ValueR(i1)});
    }
    for (size_t i = 0; i < f1->basic_blocks[bb_id]->instructions.size(); i++) {
      auto i1 = bb1->instructions[i];
      auto i2 = bb2->instructions[i];
      local_value_map.insert({fir::ValueR(i1), fir::ValueR(i2)});
      local_value_map.insert({fir::ValueR(i2), fir::ValueR(i1)});
    }
  }

  // then check if each instruction matches
  for (size_t bb_id = 0; bb_id < f1->basic_blocks.size(); bb_id++) {
    auto bb1 = f1->basic_blocks[bb_id];
    auto bb2 = f2->basic_blocks[bb_id];

    if (!match_term2(bb1->get_terminator(), bb2->get_terminator(),
                     local_value_map, difference_values)) {
      return false;
    }
    for (size_t i = 0; i < bb1->instructions.size(); i++) {
      auto i1 = bb1->instructions[i];
      auto i2 = bb2->instructions[i];
      if (i1 == i2) {
        continue;
      }
      // TODO: might need to check type here aswell
      if (i1->instr_type != i2->instr_type || i1->subtype != i2->subtype ||
          i1->args.size() != i2->args.size()) {
        return false;
      }

      if (!check_args2(i1, i2, local_value_map, difference_values)) {
        return false;
      }
    }
  }
  return true;
}

inline bool is_function_applicable(std::unique_ptr<fir::Function> &f) {
  if (f->is_decl() || f->variadic || f->get_n_uses() == 0 ||
      f->get_entry()->n_args() > 4) {
    return false;
  }
  if (f->linkage == fir::Function::Linkage::Weak ||
      f->linkage == fir::Function::Linkage::WeakODR ||
      f->linkage == fir::Function::Linkage::LinkOnce) {
    return false;
  }
  return true;
}

class FunctionDeDup final : public ModulePass {
public:
  void apply(fir::Context &ctx) override {
    ZoneScopedN("FunctionDeDup");
    TMap<fir::ValueR, fir::ValueR> local_value_map;
    TVec<DiffConst> difference_values;

    for (auto &e1 : ctx.data->storage.functions) {
      auto &f1 = e1.second;
      if (!is_function_applicable(f1)) {
        continue;
      }
      auto f1_ninstrs = f1->n_instrs();

      for (auto &e2 : ctx.data->storage.functions) {
        auto &f2 = e2.second;
        if ((void *)f1.get() == (void *)f2.get() ||
            !is_function_applicable(f2)) {
          continue;
        }

        if (f1->basic_blocks.size() != f2->basic_blocks.size() ||
            f1->get_entry()->n_args() != f2->get_entry()->n_args() ||
            f1_ninstrs != f2->n_instrs()) {
          continue;
        }

        local_value_map.clear();
        difference_values.clear();

        if (!check_match(f1, f2, local_value_map, difference_values)) {
          continue;
        }

        // TODO: heuristic
        if (difference_values.size()*4 > f1_ninstrs ||
            difference_values.size() + f1->get_entry()->n_args() > 6) {
          continue;
        }

        for (auto [i1, i2, i] : difference_values) {
          fmt::println("   MERGE1 {} {} {}", (void *)i1.get_raw_ptr(),
                       (void *)i2.get_raw_ptr(), i);
        }

        // if theres no differnces just forward all calls to the one with more
        // references
        if (difference_values.empty()) {
          fir::Function *target = f1.get();
          fir::Function *looser = f2.get();
          if (f1->get_n_uses() < f2->get_n_uses()) {
            target = f2.get();
            looser = f1.get();
          }
          TVec<fir::Use> uses{looser->get_uses().begin(),
                              looser->get_uses().end()};
          for (auto use : uses) {
            use.replace_use(
                fir::ValueR(ctx->get_constant_value(fir::FunctionR{target})));
          }
        } else {
          // TODO: Might create huge name
          auto new_name = f1->name + "_MERGED_" + f2->name;
          if (ctx->has_function(new_name.c_str())) {
            continue;
          }
          fmt::println("==========MERGING===========\n");
          fmt::println("{:d} {:d}", *f1, *f2);

          IRVec<fir::TypeR> new_arg_ty = f1->func_ty->as_func().arg_types;
          auto n_orig_args = new_arg_ty.size();
          for (auto diff : difference_values) {
            new_arg_ty.push_back(diff.i1->args[diff.arg_id].get_type());
          }

          auto new_type =
              ctx->get_func_ty(f1->func_ty->as_func().return_type, new_arg_ty);
          auto new_func =
              ctx->create_function(new_name, new_type);
          // delete the automatically inserted bb
          ASSERT(new_func->basic_blocks.size() == 1);
          new_func->basic_blocks[0]->remove_from_parent(false);
          fir::ContextData::V2VMap subs;
          for (size_t bb_id = 0; bb_id < f1->n_bbs(); bb_id++) {
            auto new_bb = ctx->copy(f1->basic_blocks.at(bb_id), subs, false);
            new_func->append_bbr(new_bb);
          }

          auto new_entry = new_func->get_entry();
          for (auto diff : difference_values) {
            new_entry.add_arg(ctx->storage.insert_bb_arg(
                new_entry, diff.i1->args[diff.arg_id].get_type()));
          }

          for (auto bb : new_func->get_bbs()) {
            for (auto instr : bb->instructions) {
              instr.substitute(subs);
            }
          }

          auto i = 0;
          for(auto diff: difference_values){
            auto instr = subs.at(fir::ValueR(diff.i1)).as_instr();
            instr.replace_arg(diff.arg_id, fir::ValueR{new_entry->args[n_orig_args + i]});
            i++;
          }

          TVec<fir::Use> uses{f1->get_uses().begin(), f1->get_uses().end()};
          auto new_func_ref = fir::ValueR(
              ctx->get_constant_value(fir::FunctionR(new_func.func)));
          for (const auto &use : uses) {
            if (use.type == fir::UseType::NormalArg && use.argId == 0) {
              auto instr = use.user;
              if (instr->is(fir::InstrType::CallInstr)) {
                instr.replace_arg(0, new_func_ref);
                for (auto diff : difference_values) {
                  instr.add_arg(diff.i1->args[diff.arg_id]);
                }
              }
            }
          }
          TVec<fir::Use> uses2{f2->get_uses().begin(), f2->get_uses().end()};
          for (const auto &use : uses2) {
            if (use.type == fir::UseType::NormalArg && use.argId == 0) {
              auto instr = use.user;
              if (instr->is(fir::InstrType::CallInstr)) {
                instr.replace_arg(0, new_func_ref);
                for (auto diff : difference_values) {
                  instr.add_arg(diff.i2->args[diff.arg_id]);
                }
              }
            }
          }
          // fmt::println("==========MERGING===========\n");
          // fmt::println("{:d} {:d}", *f1, *f2);
          // fmt::println("=====================\n{:d}", *new_func.func);
          // fmt::println("{}", difference_values.size());
          // TODO("okak");
        }
      }
    }
  }
};
} // namespace foptim::optim
