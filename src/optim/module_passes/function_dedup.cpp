#include "function_dedup.hpp"

#include <algorithm>
#include <atomic>

#include "ir/basic_block_arg.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "utils/arena.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {

namespace {

struct DiffConst {
  u32 bb_id;
  u32 instr_id;
  u32 arg_id;
};

// contains indicies to place where there is a difference
//  + the funcs that thave this difference
// NOTE: difference values are assumed to be constants only!
struct MergableGroup {
  TVec<DiffConst> diffs;
  TVec<fir::Function *> funcs;
  // position of the function the group was collected for, jobs finish in any
  // order so this keeps the merge order (and the merged names) stable
  size_t origin = 0;
};

// the group that gets merged first is the last one
bool merge_order_less(const MergableGroup &a, const MergableGroup &b) {
  if (a.funcs.size() != b.funcs.size()) {
    return a.funcs.size() < b.funcs.size();
  }
  if (a.diffs.size() != b.diffs.size()) {
    return a.diffs.size() > b.diffs.size();
  }
  return a.origin > b.origin;
}

[[nodiscard]] bool check_args2(fir::Instr i1, fir::Instr i2, u32 bb_id,
                               u32 instr_id,
                               TMap<fir::ValueR, fir::ValueR> &local_value_map,
                               TVec<DiffConst> &difference_values,
                               bool onlySame) {
  // ZoneScopedN("CheckArgs2");
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
    if (onlySame) {
      return false;
    }
    difference_values.emplace_back(bb_id, instr_id, i);
  }
  return true;
}

[[nodiscard]] bool match_term2(fir::Instr i1, fir::Instr i2, u32 bb_id,
                               u32 instr_id,
                               TMap<fir::ValueR, fir::ValueR> &local_value_map,
                               TVec<DiffConst> &difference_values,
                               bool onlySame) {
  // ZoneScopedN("MatchTerm2");
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
      // if(onlySame){
      //   return false;
      // }
      // difference_values.push_back({fir::Use::bb_arg(i1, bb_id, arg_id),
      //                              fir::Use::bb_arg(i2, bb_id, arg_id)});
    }
  }
  return check_args2(i1, i2, bb_id, instr_id, local_value_map,
                     difference_values, onlySame);
}

bool check_match(fir::Function *f1, fir::Function *f2,
                 TMap<fir::ValueR, fir::ValueR> &local_value_map,
                 TVec<DiffConst> &difference_values, bool onlySame) {
  // ZoneScopedN("CheckMatch");
  // quick and dirty first check since most wont match we can quit early

  // fill up the local value map
  // for recursive calls
  // local_value_map.insert({fir::ValueR{f1->ctx->get_constant_value(f1)},
  //                         fir::ValueR{f1->ctx->get_constant_value(f2)}});
  for (size_t bb_id = 0; bb_id < f1->basic_blocks.size(); bb_id++) {
    const auto &bb1 = f1->basic_blocks[bb_id];
    const auto &bb2 = f2->basic_blocks[bb_id];
    if (bb1->n_instrs() != bb2->n_instrs() || bb1->n_args() != bb2->n_args()) {
      return false;
    }
    for (size_t i = 0; i < bb1->n_args(); i++) {
      if (bb1->args[i]->get_type() != bb2->args[i]->get_type()) {
        return false;
      }
    }
    for (size_t instr_id = 0; instr_id < bb1->instructions.size(); instr_id++) {
      auto i1 = bb1->instructions[instr_id];
      auto i2 = bb2->instructions[instr_id];
      if (i1->instr_type != i2->instr_type || i1->subtype != i2->subtype ||
          i1->args.size() != i2->args.size() ||
          i1->bbs.size() != i2->bbs.size()) {
        return false;
      }
      if (i1.is_multi_result() || i2.is_multi_result()) {
        if (!std::ranges::equal(i1->result_types(), i2->result_types())) {
          return false;
        }
      } else if (i1.result_type() != i2.result_type()) {
        return false;
      }
    }
  }

  for (size_t bb_id = 0; bb_id < f1->basic_blocks.size(); bb_id++) {
    const auto &bb1 = f1->basic_blocks[bb_id];
    const auto &bb2 = f2->basic_blocks[bb_id];
    local_value_map.insert({fir::ValueR(bb1), fir::ValueR(bb2)});
    for (size_t i = 0; i < bb1->n_args(); i++) {
      const auto &i1 = bb1->args[i];
      const auto &i2 = bb2->args[i];
      local_value_map.insert({fir::ValueR(i1), fir::ValueR(i2)});
    }
    for (size_t i = 0; i < f1->basic_blocks[bb_id]->instructions.size(); i++) {
      auto i1 = bb1->instructions[i];
      auto i2 = bb2->instructions[i];
      if (i1->has_result()) {
        fir::map_results(i1, i2, local_value_map);
      }
    }
  }

  // then check if each instruction matches
  for (size_t bb_id = 0; bb_id < f1->basic_blocks.size(); bb_id++) {
    const auto &bb1 = f1->basic_blocks[bb_id];
    const auto &bb2 = f2->basic_blocks[bb_id];

    if (!match_term2(bb1->get_terminator(), bb2->get_terminator(), bb_id,
                     f1->n_instrs() - 1, local_value_map, difference_values,
                     onlySame)) {
      return false;
    }
    for (size_t instr_id = 0; instr_id < bb1->instructions.size() - 1;
         instr_id++) {
      auto i1 = bb1->instructions[instr_id];
      auto i2 = bb2->instructions[instr_id];
      if (!check_args2(i1, i2, bb_id, instr_id, local_value_map,
                       difference_values, onlySame)) {
        return false;
      }
    }
  }
  return true;
}

// what never changes while merging
bool is_function_mergeable(const fir::Function *f) {
  if (f->is_decl() || f->attribs.variadic || f->get_entry()->n_args() > 4) {
    return false;
  }
  // TODO: should work for linkonce odr aswell
  if (f->attribs.linkage == fir::Linkage::Weak ||
      f->attribs.linkage == fir::Linkage::WeakODR ||
      f->attribs.linkage == fir::Linkage::LinkOnce ||
      f->attribs.linkage == fir::Linkage::External) {
    return false;
  }
  return true;
}

bool is_function_applicable(const fir::Function *f) {
  return f->get_n_uses() != 0 && is_function_mergeable(f);
}

u64 mix_hash(u64 h, u64 v) {
  h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  return h;
}

u64 type_key(fir::TypeR ty) { return reinterpret_cast<u64>(ty.get_raw_ptr()); }

// Only functions with the same key can match: same type, blocks, and per
// instruction the kind, operand count and result type.
u64 structure_key(const fir::Function *f) {
  u64 h = type_key(f->func_ty);
  for (const auto &bb : f->basic_blocks) {
    h = mix_hash(h, bb->n_instrs());
    h = mix_hash(h, bb->n_args());
    for (size_t i = 0; i < bb->n_args(); i++) {
      h = mix_hash(h, type_key(bb->args[i]->get_type()));
    }
    for (auto instr : bb->instructions) {
      h = mix_hash(h, static_cast<u64>(instr->instr_type));
      h = mix_hash(h, instr->subtype);
      h = mix_hash(h, instr->args.size());
      h = mix_hash(h, instr->bbs.size());
      if (instr.is_multi_result()) {
        for (auto res_ty : instr->result_types()) {
          h = mix_hash(h, type_key(res_ty));
        }
      } else {
        h = mix_hash(h, type_key(instr.result_type()));
      }
    }
  }
  return h;
}

// The functions that can be merged sorted by key, functions with the same key
// stay in function order. A function only needs to look at the ones after it
// in its own run.
struct Candidates {
  TVec<u32> sorted;
  // position in sorted and end of the run, per function index
  TVec<u32> rank;
  TVec<u32> run_end;

  explicit Candidates(const fir::FunctionStorage &storage) {
    const auto &all_funcs = storage.all();
    TVec<std::pair<u64, u32>> keyed;
    for (u32 i = 0; i < all_funcs.size(); i++) {
      if (is_function_mergeable(all_funcs[i])) {
        keyed.emplace_back(structure_key(all_funcs[i]), i);
      }
    }
    std::ranges::sort(keyed);
    rank.assign(all_funcs.size(), 0);
    run_end.assign(all_funcs.size(), 0);
    for (u32 k = 0; k < keyed.size(); k++) {
      sorted.push_back(keyed[k].second);
      rank[keyed[k].second] = k;
    }
    for (u32 k = 0; k < keyed.size();) {
      u32 e = k;
      while (e < keyed.size() && keyed[e].first == keyed[k].first) {
        e++;
      }
      for (u32 j = k; j < e; j++) {
        run_end[keyed[j].second] = e;
      }
      k = e;
    }
  }
};

// select a fitting type based on the value types + potential constriants by the
// instruction
fir::TypeR select_type(TVec<fir::TypeR> tys, fir::Instr instr, u32 /*arg_id*/) {
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
      biggest_width < instr.result_type()->get_bitwidth()) {
    return instr.result_type();
  }

  return biggest_type;
}

// merge the functions into a new function
//  this new function taking in arguments for the difference values
// NOTE: difference values are assumed to be constants only!
// returns true on success false otherwise
bool merge_functions(MergableGroup &group, fir::Context &ctx,
                     AnalysisManager &analyMan) {
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
      ctx->get_func_ty(f1->func_ty->as_func().return_types, new_arg_ty);
  auto new_func = ctx->create_function(new_name, new_type);
  // delete the automatically inserted bb
  ASSERT(new_func->basic_blocks.size() == 1);
  new_func->basic_blocks[0]->remove_from_parent(false);
  fir::ContextData::V2VMap subs;
  for (size_t bb_id = 0; bb_id < f1->n_bbs(); bb_id++) {
    // the functions of the group may differ in their metadata
    auto new_bb = ctx->copy(f1->basic_blocks.at(bb_id), subs, false,
                            fir::MetaCopy::None, new_func.func);
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
    auto key = instr1->is_multi_result() ? instr1.result(0) : instr1.result();
    auto instr = subs.at(key).as_instr();
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
          analyMan.touched_body(*instr->get_parent()->get_parent().func);
        }
      }
    }
  }
  return true;
}

void collect_group(const fir::Context &ctx, const Candidates &cands,
                   size_t idx1, foptim::Mutex<FVec<MergableGroup>> &groups) {
  TMap<fir::ValueR, fir::ValueR> local_value_map;
  TVec<DiffConst> difference_values;

  const auto &all_funcs = ctx->storage.functions.all();
  auto *f1 = all_funcs[idx1];
  auto f1_ninstrs = f1->n_instrs();
  MergableGroup group{};
  group.origin = idx1;

  // we just use the first match to then collect a group
  //  which could be a non optimal group
  for (u32 k = cands.rank[idx1] + 1; k < cands.run_end[idx1]; k++) {
    auto *f2 = all_funcs[cands.sorted[k]];
    if (!is_function_applicable(f2)) {
      continue;
    }

    if (f1->basic_blocks.size() != f2->basic_blocks.size() ||
        f1->get_entry()->n_args() != f2->get_entry()->n_args() ||
        f1_ninstrs != f2->n_instrs() || f1->func_ty != f2->func_ty) {
      continue;
    }

    local_value_map.clear();
    difference_values.clear();

    if (!check_match(f1, f2, local_value_map, difference_values, false)) {
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
    auto groupi = groups.scoped_lock();
    groupi->push_back(group);
  }
}

}  // namespace

PreservedAnalysis merge_func_dups_only_same(fir::Context &ctx,
                                            AnalysisManager &analyMan) {
  ZoneScopedN("DeDup0Diff");
  auto saved_talloc = utils::TempAlloc<void *>::scoped();
  TMap<fir::ValueR, fir::ValueR> local_value_map;
  TVec<DiffConst> difference_values;
  TVec<fir::Use> uses;

  const auto &all_funcs = ctx.data->storage.functions.all();
  const Candidates cands{ctx.data->storage.functions};
  for (size_t iter1 = 0; iter1 < all_funcs.size(); iter1++) {
    // helpers
    auto *f1 = all_funcs[iter1];
    if (!is_function_applicable(f1)) {
      continue;
    }
    auto f1_ninstrs = f1->n_instrs();

    // we just use the first match to then collect a group
    //  which could be a non optimal group
    {
      for (u32 k = cands.rank[iter1] + 1; k < cands.run_end[iter1]; k++) {
        auto *f2 = all_funcs[cands.sorted[k]];
        if (!is_function_applicable(f2)) {
          continue;
        }

        if (f1->func_ty != f2->func_ty ||
            f1->basic_blocks.size() != f2->basic_blocks.size() ||
            f1->get_entry()->n_args() != f2->get_entry()->n_args() ||
            f1_ninstrs != f2->n_instrs()) {
          continue;
        }

        local_value_map.clear();

        if (!check_match(f1, f2, local_value_map, difference_values, true)) {
          continue;
        }
        if (!difference_values.empty()) {
          continue;
        }
        {
          // merge each pair that have no diffs since merging these does not
          // really affect stuff
          fir::Function *target = f2;
          fir::Function *other = f1;
          if (f1->attribs.linkage == fir::Linkage::Internal) {
            target = f1;
            other = f2;
          }
          if (target->attribs.linkage != fir::Linkage::Internal &&
              target->attribs.linkage != fir::Linkage::LinkOnceODR) {
            continue;
          }
          // the callers of other now run the body of target, so a type tag
          // that differs between the two is not true for both
          bool meta_changed = false;
          for (size_t bb_id = 0; bb_id < target->basic_blocks.size(); bb_id++) {
            auto t_bb = target->basic_blocks[bb_id];
            auto o_bb = other->basic_blocks[bb_id];
            for (size_t i = 0; i < t_bb->instructions.size(); i++) {
              auto t_instr = t_bb->instructions[i];
              if (!t_instr->has_access_meta) {
                continue;
              }
              const auto *t_meta = target->get_access_meta(t_instr);
              const auto *o_meta = o_bb->instructions[i]->has_access_meta
                                       ? other->get_access_meta(
                                             o_bb->instructions[i])
                                       : nullptr;
              if (t_meta != nullptr &&
                  (o_meta == nullptr || o_meta->types != t_meta->types)) {
                fir::AccessMeta m = *t_meta;
                m.types.clear();
                target->set_access_meta(t_instr, std::move(m));
                meta_changed = true;
              }
            }
          }
          if (meta_changed) {
            analyMan.touched_body(*target);
          }
          auto target_ref = ctx->get_constant_value(fir::FunctionR{target});
          uses.clear();
          uses.insert(uses.begin(), other->get_uses().begin(),
                      other->get_uses().end());
          // other->replace_all_uses(fir::ValueR{target_ref});
          for (auto use : uses) {
            use.replace_use(fir::ValueR(target_ref));
            analyMan.touched_body(*use.user->get_parent()->get_parent().func);
          }
          utils::StatCollector::get().addi(
              1, "funcDedup0Diff", utils::StatCollector::StatType::StatFOptim);
        }
      }
    }
  }
  return PreservedAnalysis::all();
}

PreservedAnalysis merge_func_dups(fir::Context &ctx, JobSheduler *shed,
                                  AnalysisManager &analyMan) {
  Mutex<FVec<MergableGroup>> prot_groups;
  // helpers

  {
    ZoneScopedN("GroupCollectioning");
    // (void)shed;
    auto n_funcs = ctx.data->storage.functions.size();
    auto *waits = utils::TempAlloc<std::atomic<bool>>{}.allocate(n_funcs);
    size_t i = 0;
    const auto &all_funcs = ctx.data->storage.functions.all();
    const Candidates cands{ctx.data->storage.functions};
    for (size_t iter1 = 0; iter1 < all_funcs.size(); iter1++) {
      auto *f1 = all_funcs[iter1];
      if (!is_function_applicable(f1)) {
        continue;
      }
      waits[i].store(false, std::memory_order_release);
      shed->push(&waits[i], [&ctx, &cands, iter1, &prot_groups]() {
        ZoneScopedN("CollectingGroup");
        collect_group(ctx, cands, iter1, prot_groups);
      });
      i++;
    }
    for (; i > 0; i--) {
      shed->wait_for(&waits[i - 1], std::memory_order_acquire);
    }
  }

  {
    auto groups_lock = prot_groups.scoped_lock();
    auto &groups = *groups_lock;
    ZoneScopedN("DiffMerging");
    // since functions could be in multiple groups we will sort them to first
    // merge the biggest groups
    std::ranges::sort(groups, merge_order_less);

    while (!groups.empty()) {
      auto &curr = groups.back();
      groups.pop_back();
      if (curr.funcs.size() > 1) {
        // fmt::println("Got group with size {} with {} diffs",
        // curr.funcs.size(),
        //              curr.diffs.size());
        if (!merge_functions(curr, ctx, analyMan)) {
          continue;
        }
        utils::StatCollector::get().addi(
            static_cast<i64>(curr.funcs.size()) - 1, "funcDedupDiff",
            utils::StatCollector::StatType::StatFOptim);
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
        std::ranges::sort(groups, merge_order_less);
      }
    }
  }
  return PreservedAnalysis::all();
}

}  // namespace foptim::optim
