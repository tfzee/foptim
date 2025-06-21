#pragma once
#include "ir/basic_block_arg.hpp"
#include "ir/function.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "optim/module_pass.hpp"
#include <fmt/core.h>

#include <algorithm>

namespace foptim::optim {

struct DiffConst {
  u32 bb_id;
  u32 instr_id;
  u32 arg_id;
};

[[nodiscard]] inline bool
check_args2(fir::Instr i1, fir::Instr i2, u32 bb_id, u32 instr_id,
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
    // else if they aren teh same + they are the same local we cancel
    // TODO: could do dynamic alloca bt idk if worth it could use cost variable
    // to keep track of this
    if (!arg1.is_constant() || !arg2.is_constant() ||
        i1->is(fir::InstrType::AllocaInstr) ||
        arg1.get_type() != arg2.get_type()) {
      return false;
    }
    if (i1->is(fir::InstrType::CallInstr) || i == 0) {
      return false;
    }
    difference_values.emplace_back(bb_id, instr_id, i);
  }
  return true;
}

[[nodiscard]] inline bool
match_term2(fir::Instr i1, fir::Instr i2, u32 bb_id, u32 instr_id,
            TMap<fir::ValueR, fir::ValueR> &local_value_map,
            TVec<DiffConst> &difference_values) {
  if (i1 == i2) {
    return true;
  }
  if (i1->instr_type != i2->instr_type || i1->subtype != i2->subtype ||
      i1->args.size() != i2->args.size()) {
    return false;
  }
  for (size_t bb_arg_id = 0; bb_arg_id < i1->bbs.size(); bb_arg_id++) {
    auto &bb1 = i1->bbs[bb_arg_id];
    auto &bb2 = i2->bbs[bb_arg_id];
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
      // if (!a1.is_constant() || !a2.is_constant()) {
      return false;
      // }
      // difference_values.push_back({fir::Use::bb_arg(i1, bb_id, arg_id),
      //                              fir::Use::bb_arg(i2, bb_id, arg_id)});
    }
  }
  return check_args2(i1, i2, bb_id, instr_id, local_value_map,
                     difference_values);
}

inline bool check_match(fir::Function *f1, fir::Function *f2,
                        TMap<fir::ValueR, fir::ValueR> &local_value_map,
                        TVec<DiffConst> &difference_values) {
  // quick and dirty first check since most wont match we can quit early

  // fill up the local value map
  // for recursive calls
  local_value_map.insert({fir::ValueR{f1->ctx->get_constant_value(f1)},
                          fir::ValueR{f1->ctx->get_constant_value(f2)}});
  for (size_t bb_id = 0; bb_id < f1->basic_blocks.size(); bb_id++) {
    auto bb1 = f1->basic_blocks[bb_id];
    auto bb2 = f2->basic_blocks[bb_id];
    local_value_map.insert({fir::ValueR(bb1), fir::ValueR(bb2)});
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
    }
    for (size_t i = 0; i < f1->basic_blocks[bb_id]->instructions.size(); i++) {
      auto i1 = bb1->instructions[i];
      auto i2 = bb2->instructions[i];
      local_value_map.insert({fir::ValueR(i1), fir::ValueR(i2)});
    }
  }

  // then check if each instruction matches
  for (size_t bb_id = 0; bb_id < f1->basic_blocks.size(); bb_id++) {
    auto bb1 = f1->basic_blocks[bb_id];
    auto bb2 = f2->basic_blocks[bb_id];

    if (!match_term2(bb1->get_terminator(), bb2->get_terminator(), bb_id,
                     f1->n_instrs() - 1, local_value_map, difference_values)) {
      return false;
    }
    for (size_t instr_id = 0; instr_id < bb1->instructions.size() - 1;
         instr_id++) {
      auto i1 = bb1->instructions[instr_id];
      auto i2 = bb2->instructions[instr_id];
      if (i1 == i2) {
        continue;
      }
      // TODO: might need to check type here aswell
      if (i1->instr_type != i2->instr_type || i1->subtype != i2->subtype ||
          i1->args.size() != i2->args.size()) {
        return false;
      }

      if (!check_args2(i1, i2, bb_id, instr_id, local_value_map,
                       difference_values)) {
        return false;
      }
    }
  }
  return true;
}

inline bool is_function_applicable(const fir::Function *f) {
  if (f->is_decl() || f->variadic || f->get_n_uses() == 0 ||
      f->get_entry()->n_args() > 4) {
    return false;
  }
  // TODO: should work for linkonce odr aswell
  if (f->linkage == fir::Linkage::Weak || f->linkage == fir::Linkage::WeakODR ||
      f->linkage == fir::Linkage::LinkOnce ||
      f->linkage == fir::Linkage::External) {
    return false;
  }
  return true;
}

// contains indicies to place where there is a difference
//  + the funcs that thave this difference
// NOTE: difference values are assumed to be constants only!
struct MergableGroup {
  TVec<DiffConst> diffs;
  TVec<fir::Function *> funcs;
};

// select a fitting type based on the value types + potential constriants by the
// instruction
inline fir::TypeR select_type(TVec<fir::TypeR> tys, fir::Instr instr,
                              u32 /*arg_id*/) {
  fir::TypeR biggest_type = tys[0];
  u32 biggest_width = tys[0]->get_bitwidth();
  bool all_the_same = true;
  for (auto ty : tys) {
    if (ty->get_bitwidth() > biggest_width) {
      biggest_width = biggest_type->get_bitwidth();
      biggest_type = ty;
    }
    if (tys[0] != ty) {
      all_the_same = false;
    }
  }
  if (all_the_same) {
    return biggest_type;
  }
  if ((instr->is(fir::InstrType::UnaryInstr) ||
       instr->is(fir::InstrType::BinaryInstr)) &&
      biggest_width < instr.get_type()->get_bitwidth()) {
    return instr.get_type();
  }

  return biggest_type;
}

// merge the functions into a new function
//  this new function taking in arguments for the difference values
// NOTE: difference values are assumed to be constants only!
// returns true on success false otherwise
inline bool merge_functions(MergableGroup &group, fir::Context &ctx) {
  // TODO: Might create huge name

  auto diff_size = group.diffs.size();
  auto func_size = group.funcs.size();
  // NOTE: this is cancer with the tostring + c_str
  auto new_name = group.funcs.back()->name + "_MERGED_" +
                  std::to_string(func_size).c_str() + "_" +
                  std::to_string(diff_size).c_str();
  if (ctx->has_function(new_name.c_str())) {
    return false;
  }
  auto &f1 = group.funcs.back();

  // TODO prealloc to not have any reallocs
  IRVec<fir::TypeR> new_arg_ty = f1->func_ty->as_func().arg_types;
  auto n_orig_args = new_arg_ty.size();
  TVec<fir::TypeR> new_types;
  new_types.reserve(group.diffs.size());
  TVec<fir::TypeR> all_the_types;
  for (auto diff : group.diffs) {
    all_the_types.clear();
    for (auto *f : group.funcs) {
      auto typee = f->basic_blocks[diff.bb_id]
                       ->instructions[diff.instr_id]
                       ->args[diff.arg_id]
                       .get_type();
      all_the_types.push_back(typee);
    }
    auto res_type = select_type(
        all_the_types,
        f1->basic_blocks[diff.bb_id]->instructions[diff.instr_id], diff.arg_id);

    new_arg_ty.push_back(res_type);
    new_types.push_back(res_type);
  }

  auto new_type =
      ctx->get_func_ty(f1->func_ty->as_func().return_type, new_arg_ty);
  auto new_func = ctx->create_function(new_name, new_type);
  // delete the automatically inserted bb
  ASSERT(new_func->basic_blocks.size() == 1);
  new_func->basic_blocks[0]->remove_from_parent(false);
  fir::ContextData::V2VMap subs;
  for (size_t bb_id = 0; bb_id < f1->n_bbs(); bb_id++) {
    auto new_bb = ctx->copy(f1->basic_blocks.at(bb_id), subs, false);
    new_func->append_bbr(new_bb);
  }

  auto new_entry = new_func->get_entry();
  for (auto typee : new_types) {
    new_entry.add_arg(ctx->storage.insert_bb_arg(new_entry, typee));
  }

  for (auto bb : new_func->get_bbs()) {
    for (auto instr : bb->instructions) {
      instr.substitute(subs);
    }
  }

  auto i = 0;
  for (auto diff : group.diffs) {
    auto instr1 = f1->basic_blocks[diff.bb_id]->instructions[diff.instr_id];
    auto instr = subs.at(fir::ValueR(instr1)).as_instr();
    instr.replace_arg(diff.arg_id,
                      fir::ValueR{new_entry->args[n_orig_args + i]});
    i++;
  }

  auto new_func_ref =
      fir::ValueR(ctx->get_constant_value(fir::FunctionR(new_func.func)));

  // TODO: prob should mvoe this out
  TVec<fir::Use> uses;
  for (auto *f : group.funcs) {
    uses.clear();
    uses.assign(f->get_uses().begin(), f->get_uses().end());
    for (const auto &use : uses) {
      if (use.type == fir::UseType::NormalArg && use.argId == 0) {
        auto instr = use.user;
        if (instr->is(fir::InstrType::CallInstr)) {
          instr.replace_arg(0, new_func_ref);
          for (auto diff : group.diffs) {
            instr.add_arg(f->basic_blocks[diff.bb_id]
                              ->instructions[diff.instr_id]
                              ->args[diff.arg_id]);
          }
        }
      }
    }
  }
  return true;
}

class FunctionDeDup final : public ModulePass {
public:
  void apply(fir::Context &ctx) override {
    ZoneScopedN("FunctionDeDup");
    TVec<MergableGroup> groups;
    TMap<fir::ValueR, fir::ValueR> local_value_map;
    TVec<DiffConst> difference_values;
    // helpers
    for (auto iter1 = ctx.data->storage.functions.begin();
         iter1 != ctx.data->storage.functions.end(); iter1++) {
      auto *f1 = iter1->second.get();
      if (!is_function_applicable(f1)) {
        continue;
      }
      auto f1_ninstrs = f1->n_instrs();
      MergableGroup group{};

      auto iter2 = std::next(iter1);
      // we just use the first match to then collect a group
      //  which could be a non optimal group
      for (; iter2 != ctx.data->storage.functions.end(); iter2++) {
        auto *f2 = iter2->second.get();
        if (!is_function_applicable(f2)) {
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
        if (difference_values.size() * 4 > f1_ninstrs ||
            difference_values.size() + f1->get_entry()->n_args() > 6) {
          continue;
        }
        if (group.funcs.empty()) {
          // dont need to copy it if there was no diff
          if (!difference_values.empty()) {
            group.diffs = difference_values;
          }
        } else {
          if (group.diffs.size() != difference_values.size()) {
            continue;
          }
          bool matched = true;
          for (size_t i = 0; i < group.diffs.size(); i++) {
            if (group.diffs[i].arg_id != difference_values[i].arg_id ||
                group.diffs[i].bb_id != difference_values[i].bb_id ||
                group.diffs[i].instr_id != difference_values[i].instr_id) {
              matched = false;
              break;
            }
          }
          if (!matched) {
            continue;
          }
        }

        group.funcs.push_back(f2);
      }
      if (group.funcs.size() > 0) {
        group.funcs.push_back(f1);
        groups.push_back(group);
      }
    }

    // merge all groups that have no diffs since merging these does not really
    // affect what can be merged only might reduce the number of mergable things
    for (size_t g_id = 0; g_id < groups.size(); g_id++) {
      auto &g = groups[g_id];
      if (g.diffs.size() == 0 && g.funcs.size() > 1) {
        fir::Function *target = g.funcs.back();
        // only works if the target one is not linkOnceODR
        if (target->linkage != fir::Linkage::Internal) {
          continue;
        }
        for (auto f2 = g.funcs.begin(); f2 != std::prev(g.funcs.end()); f2++) {
          TVec<fir::Use> uses{(*f2)->get_uses().begin(),
                              (*f2)->get_uses().end()};
          for (auto use : uses) {
            use.replace_use(
                fir::ValueR(ctx->get_constant_value(fir::FunctionR{target})));
          }
          // clena up from other groups by removing f2
          for (size_t g2_id = 0; g2_id < groups.size(); g2_id++) {
            if (g2_id == g_id) {
              continue;
            }
            auto res = std::find(groups[g2_id].funcs.begin(),
                                 groups[g2_id].funcs.end(), *f2);
            if (res != groups[g2_id].funcs.end()) {
              groups[g2_id].funcs.erase(res);
              continue;
            }
          }
        }
        groups.erase(groups.begin() + g_id);
        g_id--;
      }
    }

    // since functions could be in multiple groups we will sort them to first
    // merge the biggest groups
    std::ranges::sort(groups, [](auto &a, auto &b) {
      auto a_size = a.funcs.size();
      auto b_size = b.funcs.size();
      if (a_size == b_size) {
        return a.diffs.size() > b.diffs.size();
      }
      return a_size < b_size;
    });

    while (!groups.empty()) {
      auto &curr = groups.back();
      groups.pop_back();
      if (curr.funcs.size() > 1) {
        // fmt::println("Got group with size {} with {} diffs",
        // curr.funcs.size(),
        //              curr.diffs.size());
        if (!merge_functions(curr, ctx)) {
          continue;
        }
        // clena up from other groups by removing f2
        for (size_t g2_id = 0; g2_id + 1 < groups.size(); g2_id++) {
          for (auto f2 = curr.funcs.begin(); f2 != std::prev(curr.funcs.end());
               f2++) {
            auto res = std::find(groups[g2_id].funcs.begin(),
                                 groups[g2_id].funcs.end(), *f2);
            if (res != groups[g2_id].funcs.end()) {
              groups[g2_id].funcs.erase(res);
              continue;
            }
          }
        }

        // after we did cleanup we should resort
        std::ranges::sort(groups, [](auto &a, auto &b) {
          auto a_size = a.funcs.size();
          auto b_size = b.funcs.size();
          if (a_size == b_size) {
            return a.diffs.size() > b.diffs.size();
          }
          return a_size < b_size;
        });
      }
    }
  }
};
} // namespace foptim::optim
