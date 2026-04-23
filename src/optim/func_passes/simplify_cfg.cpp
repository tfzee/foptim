#include "simplify_cfg.hpp"

#include <fmt/base.h>
#include <fmt/core.h>
#include <fmt/std.h>

#include <algorithm>

#include "ir/basic_block.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/helper/helper.hpp"
#include "optim/helper/inline.hpp"
#include "utils/arena.hpp"
#include "utils/bitset.hpp"
#include "utils/set.hpp"
#include "utils/stable_vec_ref.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {
SimplifyCFG::Res SimplifyCFG::flip_cold_cond(CFG &cfg, CFG::Node &curr) {
  (void)cfg;
  auto term = curr.bb->get_terminator();
  if (!term->is(fir::InstrType::CondBranchInstr)) {
    return SimplifyCFG::Res::NoChange;
  }
  auto &tru_target = term->bbs[0];
  auto &fals_target = term->bbs[1];

  // TODO: could detect other kinds of cold stuff(maybe just apply it on every
  // return for now that atleast in loops shold be correct)
  if (tru_target.bb->get_terminator()->is(fir::InstrType::Unreachable)) {
    return SimplifyCFG::Res::NoChange;
  }
  if (!fals_target.bb->get_terminator()->is(fir::InstrType::Unreachable)) {
    return SimplifyCFG::Res::NoChange;
  }

  flip_cond_branch(term);
  return SimplifyCFG::Res::Changed;
}

SimplifyCFG::Res SimplifyCFG::remove_struct_bb_arg(CFG &cfg, CFG::Node &curr) {
  (void)cfg;
  auto *ctx = curr.bb->get_parent()->ctx;
  TVec<fir::BBArgument> args{curr.bb->args.begin(), curr.bb->args.end()};
  for (size_t arg_id = 0; arg_id < args.size(); arg_id++) {
    auto arg = args[arg_id];
    if (!arg->_type->is_struct()) {
      continue;
    }
    auto res_ty = arg->_type;
    auto stru_ty = res_ty->as_struct();
    TVec<fir::ValueR> new_args;
    for (auto &member : stru_ty.elems) {
      auto new_arg =
          ctx->storage.insert_bb_arg(fir::BBArgumentData{curr.bb, member.ty});
      curr.bb.add_arg(new_arg);
      new_args.emplace_back(new_arg);
    }
    // setup the destructuring at the branch sites
    {
      for (auto _use : curr.bb->get_uses()) {
        auto user = _use.user;
        auto bb_arg_id = user.get_bb_id(curr.bb);
        fir::Builder buh{user->get_parent()};
        buh.at_penultimate(user->get_parent());
        for (size_t i = 0; i < stru_ty.elems.size(); i++) {
          fir::ValueR indicies[1] = {fir::ValueR{ctx->get_constant_int(i, 32)}};
          auto extr_val =
              buh.build_extract_value(user->bbs[bb_arg_id].args[arg_id],
                                      {indicies}, stru_ty.elems[i].ty);
          user.add_bb_arg(bb_arg_id, extr_val);
        }
        user.remove_bb_arg(bb_arg_id, arg_id);
      }
    }
    // setup the constructing at the end
    {
      fir::Builder buh{curr.bb};
      buh.at_start(curr.bb);
      fir::ValueR curr_v = fir::ValueR{ctx->get_poisson_value(res_ty)};
      for (size_t i = 0; i < stru_ty.elems.size(); i++) {
        fir::ValueR indicies[1] = {fir::ValueR{ctx->get_constant_int(i, 32)}};
        curr_v =
            buh.build_insert_value(curr_v, new_args[i], {indicies}, res_ty);
      }
      arg->replace_all_uses(curr_v);
    }
    curr.bb->remove_arg(arg_id);
    // for (auto use : curr.bb->get_uses()) {
    //   fmt::println("{:cd}", use.user->get_parent());
    // }
    // fmt::println("===================={:cd}", arg);
    // fmt::println("{:cd}", curr.bb);
    // TODO("okak");
    return SimplifyCFG::Res::Changed;
  }
  return SimplifyCFG::Res::NoChange;
}

SimplifyCFG::Res SimplifyCFG::static_select_call_into_branch(
    fir::Function &func, CFG::Node &curr) {
  auto *ctx = func.ctx;
  for (auto i : curr.bb->instructions) {
    if (!i->is(fir::InstrType::CallInstr) || !i->args[0].is_instr()) {
      continue;
    }
    auto func_arg = i->args[0].as_instr();
    if (!func_arg->is(fir::InstrType::SelectInstr) ||
        !func_arg->args[1].is_constant() || !func_arg->args[2].is_constant() ||
        func_arg->get_n_uses() != 1) {
      continue;
    }
    auto ptr1 = func_arg->args[1].as_constant();
    auto ptr2 = func_arg->args[2].as_constant();
    if (!ptr1->is_func() || !ptr2->is_func() || ptr1->as_func()->is_decl() ||
        ptr2->as_func()->is_decl()) {
      continue;
    }
    auto call_res_used = i->get_n_uses() > 0;

    fir::Builder buh{i};
    auto start_bb = i->get_parent();
    auto true_bb = buh.append_bb();
    auto false_bb = buh.append_bb();
    auto end_bb = split_block(i);

    if (call_res_used) {
      auto res_arg = ctx->storage.insert_bb_arg(
          fir::BBArgumentData{end_bb, i->get_type()});
      end_bb.add_arg(res_arg);
      i->replace_all_uses(fir::ValueR{res_arg});
    }

    buh.at_end(start_bb);
    buh.build_cond_branch(func_arg->args[0], true_bb, false_bb);

    {
      buh.at_end(true_bb);
      auto call_res =
          buh.build_call(func_arg->args[1], ptr1->as_func()->func_ty,
                         i->get_type(), {++i->args.begin(), i->args.end()});
      auto b = buh.build_branch(end_bb);
      if (call_res_used) {
        b.add_bb_arg(0, call_res);
      }
    }

    {
      buh.at_end(false_bb);
      auto call_res =
          buh.build_call(func_arg->args[2], ptr1->as_func()->func_ty,
                         i->get_type(), {++i->args.begin(), i->args.end()});
      auto b = buh.build_branch(end_bb);
      if (call_res_used) {
        b.add_bb_arg(0, call_res);
      }
    }

    i.destroy();
    func_arg.destroy();
    return SimplifyCFG::Res::NeedUpdate;
  }
  return SimplifyCFG::Res::NoChange;
}

SimplifyCFG::Res SimplifyCFG::remove_dead_bb(CFG &cfg, Dominators &dom,
                                             CFG::Node &curr,
                                             fir::Function &func, size_t bb_id,
                                             bool is_entry) {
  if (is_entry) {
    return Res::NoChange;
  }

  bool is_dead = false;
  if (curr.pred.empty()) {
    is_dead = true;
  }
  // or all bs that jump into this bb are all dominated by this bb
  if (!is_dead) {
    is_dead = true;
    for (auto pred : curr.pred) {
      if (dom.dominates(bb_id, pred)) {
        continue;
      }
      is_dead = false;
      break;
    }
  }

  if (is_dead) {
    ZoneScopedN("rem dead bb");
    auto *ctx = func.ctx;
    for (auto i : func.basic_blocks[bb_id]->args) {
      if (i->get_n_uses() > 0) {
        i->replace_all_uses(fir::ValueR{ctx->get_poisson_value(i->get_type())});
      }
    }
    for (auto i : func.basic_blocks[bb_id]->instructions) {
      if (i->get_n_uses() > 0) {
        i->replace_all_uses(fir::ValueR{ctx->get_poisson_value(i.get_type())});
      }
    }
    func.basic_blocks[bb_id]->remove_from_parent(true, true, true);
    cfg.update_delete(bb_id);
    dom.update(cfg);
    return Res::Changed;
  }
  return Res::NoChange;
}

namespace {
// check for true uses (so a  bb arg  that is actually used in an instruction)
//  we might have a loop of multiple bbs that just forward a bb so it will
//  have uses but all are just forwarding to the next bb
bool has_true_use(fir::BBArgument v) {
  TVec<fir::Use> worklist;
  TSet<fir::Use> seen;
  for (auto use : v->get_uses()) {
    worklist.push_back(use);
  }
  while (!worklist.empty()) {
    auto item = worklist.back();
    worklist.pop_back();

    if (seen.contains(item)) {
      continue;
    }
    seen.insert(item);

    if (item.type == fir::UseType::NormalArg) {
      if (!item.user->is_critical() && !item.user->pot_modifies_mem() &&
          !item.user->has_pot_sideeffects()) {
        for (auto use : item.user->get_uses()) {
          worklist.push_back(use);
        }
      } else {
        return true;
      }
    } else if (item.type == fir::UseType::BB) {
      UNREACH();
    } else if (item.type == fir::UseType::BBArg) {
      auto new_item = item.user->bbs[item.argId].bb->args[item.bbArgId];
      for (auto use : new_item->get_uses()) {
        worklist.push_back(use);
      }
    }
  }
  return false;
}
}  // namespace

bool SimplifyCFG::remove_dead_bb_arg(CFG::Node &curr, fir::Function &func,
                                     bool is_entry) {
  if (curr.bb->n_args() != 0 && !is_entry) {
    ZoneScopedN("rem dead bb arg");
    auto n_args = curr.bb->n_args();
    for (u32 ip1 = n_args; ip1 > 0; ip1--) {
      auto i = ip1 - 1;
      auto &value = curr.bb->args[i];
      if (value->get_n_uses() == 0) {
        // remove from all uses where we jump into this bb
        for (auto use : curr.bb->get_uses()) {
          ASSERT(use.type == fir::UseType::BB);
          use.user.remove_bb_arg(use.argId, i);
        }

        // drop this arg we cant have any uses
        curr.bb->args.erase(curr.bb->args.begin() + i);
        return true;
      }
      if (!has_true_use(value)) {
        for (auto use : curr.bb->get_uses()) {
          ASSERT(use.type == fir::UseType::BB);
          use.user.remove_bb_arg(use.argId, i);
        }

        value->replace_all_uses(
            fir::ValueR{func.ctx->get_poisson_value(value->get_type())});

        curr.bb->args.erase(curr.bb->args.begin() + i);
        return true;
      }
    }
  }
  return false;
}

namespace {

struct DiffValues {
  fir::Use use1;
  fir::ValueR old_val;
  fir::Use use2;
};

[[nodiscard]] bool check_args(fir::Instr i1, fir::Instr i2,
                              TMap<fir::Instr, fir::Instr> &local_value_map,
                              size_t &cost,
                              TVec<DiffValues> &difference_values) {
  for (u32 i = 0; i < i1->args.size(); i++) {
    auto &arg1 = i1->args[i];
    auto &arg2 = i2->args[i];
    if (arg1 == arg2) {
      continue;
    }
    // if either is a local arg
    //  then both need to be and both need topoint to the same
    if (arg1.is_instr() && arg2.is_instr()) {
      auto arg1I = arg1.as_instr();
      auto arg2I = arg2.as_instr();
      if (local_value_map.contains(arg1I)) {
        if (local_value_map.at(arg1I) == arg2I) {
          continue;
        }
        return false;
      }
      if (arg2I->get_parent() == i2->get_parent()) {
        return false;
      }
    } else if (arg1.is_instr() && !arg2.is_instr()) {
      auto arg1I = arg1.as_instr();
      if (arg1I->get_parent() == i1->get_parent()) {
        return false;
      }
    } else if (!arg1.is_instr() && arg2.is_instr()) {
      auto arg2I = arg2.as_instr();
      if (arg2I->get_parent() == i2->get_parent()) {
        return false;
      }
    }

    difference_values.push_back(
        {fir::Use::norm(i1, i), i1->args[i], fir::Use::norm(i2, i)});
    cost += 1;
  }
  return true;
}

[[nodiscard]] bool match_term(fir::Instr i1, fir::Instr i2,
                              TMap<fir::Instr, fir::Instr> &local_value_map,
                              size_t &cost,
                              TVec<DiffValues> &difference_values) {
  if (i1 == i2) {
    return true;
  }
  if (i1->instr_type != i2->instr_type || i1->subtype != i2->subtype ||
      i1->args.size() != i2->args.size() || i1->bbs.size() != i2->bbs.size()) {
    return false;
  }
  for (size_t bb_id = 0; bb_id < i1->bbs.size(); bb_id++) {
    auto &bb1 = i1->bbs[bb_id];
    auto &bb2 = i2->bbs[bb_id];
    if (bb1.bb != bb2.bb) {
      // if we jump to different targs abort
      // NOTE: if we wouldnt do this wwe would atleast need to check that they
      // dont jump to eachother (i think)
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
      if ((!a1.is_constant() &&
           (a1.is_instr() && a1.as_instr()->get_parent() == i1->parent)) ||
          (!a2.is_constant() &&
           (a2.is_instr() && a2.as_instr()->get_parent() == i2->parent))) {
        return false;
      }
      if (a1.is_instr() && a2.is_instr()) {
        auto a1I = a1.as_instr();
        auto a2I = a2.as_instr();
        if (local_value_map.contains(a1I)) {
          if (local_value_map.at(a1I) == a2I) {
            continue;
          }
          return false;
        }
        if (a2I->get_parent() == i2->get_parent()) {
          return false;
        }
      }
      difference_values.push_back({fir::Use::bb_arg(i1, bb_id, arg_id), a1,
                                   fir::Use::bb_arg(i2, bb_id, arg_id)});
      cost += 1;
    }
  }
  auto res = check_args(i1, i2, local_value_map, cost, difference_values);
  if (i1->is(fir::InstrType::ReturnInstr)) {
    cost = std::max(1UL, cost) - 1;
  }
  return res;
}

bool dup_bb_to_args_per_bb(fir::BasicBlock bb1, fir::Function &func,
                           size_t &bb_id, TVec<DiffValues> &difference_values,
                           TMap<fir::Instr, fir::Instr> &local_value_map,
                           TVec<fir::BBArgument> &new_bb_args_helper,
                           CFG &cfg) {
  constexpr u32 N_INSTRS_UPPERBOUND = 20;
  auto *ctx = func.ctx;
  bool modified = false;
  const auto bb1n_instr = bb1->instructions.size();

  // TODO: SUPPORT old bbargs!
  if (bb1->n_args() != 0 || bb1->n_instrs() > N_INSTRS_UPPERBOUND) {
    return false;
  }

  for (size_t bb2_id = 1; bb2_id < bb_id; bb2_id++) {
    // if (bb1->get_terminator()->is(fir::InstrType::CondBranchInstr)) {
    //   continue;
    // }
    auto &bb2 = func.basic_blocks[bb2_id];
    if (bb2->n_args() != 0 ||
        bb1->instructions.size() != bb2->instructions.size()) {
      continue;
    }
    bool found = true;
    for (size_t i = 0; i < bb1->instructions.size(); i++) {
      auto i1 = bb1->instructions[i];
      auto i2 = bb2->instructions[i];
      if (i1->instr_type != i2->instr_type || i1->subtype != i2->subtype ||
          i1->args.size() != i2->args.size() ||
          i1->get_type() != i2->get_type()) {
        found = false;
        break;
      }
    }
    if (!found) {
      continue;
    }
    size_t cost = 0;
    local_value_map.clear();
    for (size_t i = 0; i < bb1n_instr; i++) {
      auto i1 = bb1->instructions[i];
      auto i2 = bb2->instructions[i];
      local_value_map.insert({i1, i2});
    }
    // setup local value map this allows us to check if it references the same
    // local instruction
    difference_values.clear();
    auto term1 = bb1->get_terminator();
    auto term2 = bb2->get_terminator();
    if (!match_term(term1, term2, local_value_map, cost, difference_values)) {
      found = false;
      continue;
    }

    for (size_t i = 0; i < bb1n_instr; i++) {
      if (difference_values.size() > 4) {
        found = false;
        break;
      }
      auto i1 = bb1->instructions[i];
      auto i2 = bb2->instructions[i];
      if (!check_args(i1, i2, local_value_map, cost, difference_values)) {
        found = false;
        break;
      }
    }
    if (std::ranges::equal(cfg.bbrs[cfg.get_bb_id(bb1)].pred,
                           cfg.bbrs[cfg.get_bb_id(bb2)].pred)) {
      // TODO: modify costs
      auto p = cfg.bbrs[cfg.get_bb_id(bb1)].pred.size() * 2;
      cost = std::max(cost, p) - p;
    }
    if (found && cost * 4 <= bb1n_instr) {
      fir::BasicBlock res_bb1 = bb1;
      fir::BasicBlock res_bb2 = bb2;
      new_bb_args_helper.clear();

      for (auto &diff : difference_values) {
        auto new_arg = ctx->storage.insert_bb_arg(
            fir::BBArgumentData{res_bb1, diff.use1.get_type()});
        res_bb1.add_arg(new_arg);
        diff.use1.replace_use(fir::ValueR{new_arg});
      }

      for (auto u1 : res_bb1->uses) {
        ASSERT(u1.type == fir::UseType::BB);
        // auto &bb_ref = u1.user->bbs[u1.argId];
        for (auto &diff : difference_values) {
          u1.user.add_bb_arg(u1.argId, diff.old_val);
          // bb_ref.args.push_back(diff.old_val);
        }
      }
      for (auto u2 : res_bb2->uses) {
        ASSERT(u2.type == fir::UseType::BB);
        for (auto &diff : difference_values) {
          u2.user.add_bb_arg(u2.argId, diff.use2.get_value());
        }
      }
      res_bb2->replace_all_uses(fir::ValueR(res_bb1));
      ASSERT(res_bb2->get_n_uses() == 0);
      res_bb2->remove_from_parent(true, true, true);
      utils::StatCollector::get().addi(1, "MergedBBBlocks");
      bb2_id--;
      bb_id--;
      modified = true;
      // return false;
      continue;
    }
  }

  return modified;
}
}  // namespace

bool SimplifyCFG::dup_bb_to_args(fir::Function &func, CFG &cfg) {
  ZoneScopedN("dup bb to arg");
  bool modified = false;
  TVec<DiffValues> difference_values;
  TMap<fir::Instr, fir::Instr> local_value_map;
  TVec<fir::BBArgument> new_bb_args_helper;
  // difference_values.reserve(32);
  // new_bb_args_helper.reserve(32);

  for (size_t bb_id = 1; bb_id < func.basic_blocks.size(); bb_id++) {
    bool mod = dup_bb_to_args_per_bb(func.basic_blocks[bb_id], func, bb_id,
                                     difference_values, local_value_map,
                                     new_bb_args_helper, cfg);
    if (mod) {
      cfg = CFG{func};
    }
    modified |= mod;
  }
  return modified;
}

bool SimplifyCFG::remove_useless_bb_args(CFG &cfg, CFG::Node &curr) {
  if (curr.pred.size() == 1 && curr.bb->n_args() != 0) {
    ZoneScopedN("rem useless bb args");
    auto n_args = curr.bb->n_args();
    auto pred_term = cfg.bbrs[curr.pred[0]].bb->get_terminator();
    auto pred_term_bb_id = pred_term.get_bb_id(curr.bb);
    for (u32 i = 0; i < n_args; i++) {
      curr.bb->args[i]->replace_all_uses(
          pred_term->bbs[pred_term_bb_id].args[i]);
    }
    curr.bb->clear_args();
    pred_term.clear_bb_args(pred_term_bb_id);
    return true;
  }
  return false;
}

bool SimplifyCFG::remove_dup_bb_args(CFG::Node &curr, bool is_entry) {
  if (curr.bb->n_args() < 2 || is_entry || curr.pred.size() == 0) {
    return false;
  }
  ZoneScopedN("rem dup bb args");
  TVec<std::pair<u32, u32>> dup_pairs;
  // go through one of the users and check its args
  //  if there are duplicates, store them into dup_pairs as potential duplicates
  // we first find any dups one one input and then later verify that its true
  // duplicateas
  const auto &use0 = curr.bb->uses[0];
  const auto &args_use0 = use0.user->bbs[use0.argId].args;
  for (u32 arg1_id = 0; arg1_id < args_use0.size(); arg1_id++) {
    u32 latest_dup = 0;
    for (u32 arg2_id = arg1_id + 1; arg2_id < args_use0.size(); arg2_id++) {
      if (args_use0[arg1_id] == args_use0[arg2_id]) {
        latest_dup = arg2_id;
      }
    }
    // we store the latest dup so we have only every dup once in here instead of
    // multiple times
    if (latest_dup != 0) {
      dup_pairs.emplace_back(latest_dup, arg1_id);
    }
  }
  if (dup_pairs.empty()) {
    return false;
  }
  // now we need to verify if these are duplicates in all uses
  for (size_t usei = 1; usei < curr.bb->uses.size(); usei++) {
    auto &use = curr.bb->uses[usei];
    auto &args_use = use.user->bbs[use.argId].args;
    for (size_t ip1 = dup_pairs.size(); ip1 > 0; ip1--) {
      auto &p = dup_pairs[ip1 - 1];
      if (args_use[p.first] != args_use[p.second]) {
        dup_pairs.erase(dup_pairs.begin() + (ip1 - 1));
        continue;
      }
    }
  }
  if (dup_pairs.empty()) {
    return false;
  }
  // since the ordering is not really nice since we could have duplicates
  //  that are sorounded by another duplicate pair
  //  we first do cleanup and set the duplicates to only use the second of the
  //  pair then since the first is ordered nicely we can just delete them
  // first cleanup all the uses of the first
  for (auto [a, b] : dup_pairs) {
    curr.bb->args[a]->replace_all_uses(fir::ValueR{curr.bb->args[b]});
  }
  // then cleanup all the inputs and a itself
  // since we can have multiple thingies that we need to remove we need to
  // deduplicate them
  TSet<u32> dupls;
  for (auto &[a, _] : dup_pairs) {
    dupls.insert(a);
  }
  for (auto &use : curr.bb->uses) {
    for (auto d : dupls) {
      use.user.remove_bb_arg(use.argId, d);
    }
  }
  for (auto d : dupls) {
    curr.bb->remove_arg(d);
  }

  // for (auto [a, b] : dup_pairs) {
  //   fmt::println("{{{}:{}}}", a, b);
  // }
  // for (auto &use : curr.bb->uses) {
  //   fmt::println("{:cd}", use.user->get_parent());
  // }
  // fmt::println("{:cd}", curr.bb);

  // TODO("impl");
  return true;
}
bool SimplifyCFG::remove_constant_bb_args(CFG::Node &curr, bool is_entry) {
  if (curr.bb->n_args() != 0 && !is_entry) {
    ZoneScopedN("rem const bb args");
    auto n_args = curr.bb->n_args();
    for (u32 ip1 = n_args; ip1 > 0; ip1--) {
      auto i = ip1 - 1;
      fir::ValueR c_value{};
      bool is_c = true;

      TVec<fir::Use> uses{curr.bb->uses.begin(), curr.bb->uses.end()};

      for (auto use : uses) {
        ASSERT(use.type == fir::UseType::BB);
        auto pred_term = use.user;
        auto pred_term_bb_id = use.argId;
        auto incoming_arg = pred_term->bbs[pred_term_bb_id].args[i];
        if (c_value.is_invalid() || incoming_arg.eql(c_value)) {
          c_value = incoming_arg;
        } else if (incoming_arg.is_bb_arg() &&
                   incoming_arg.as_bb_arg() == curr.bb->args[i]) {
        } else {
          is_c = false;
          break;
        }
      }
      if (is_c) {
        for (auto use : uses) {
          ASSERT(use.type == fir::UseType::BB);
          auto user = use.user;
          auto bb_id = user.get_bb_id(curr.bb);
          user.remove_bb_arg(bb_id, i);
        }
        curr.bb->args[i]->replace_all_uses(c_value);
        curr.bb->args.erase(curr.bb->args.begin() + i);
        return true;
      }
    }
  }
  return false;
}

bool SimplifyCFG::remove_unreach(CFG &cfg, CFG::Node &curr, bool is_entry) {
  if (is_entry) {
    return false;
  }
  if (!curr.bb->get_terminator()->is(fir::InstrType::Unreachable)) {
    return false;
  }
  ZoneScopedN("rem unreach");
  // TODO: right now if it can diverge we cancel the transformation
  //  but we could instead only delete everything after the last point at which
  //  it could diverge
  for (auto instr : curr.bb->instructions) {
    // just switching to make sure to update this
    switch (instr->instr_type) {
      case fir::InstrType::CallInstr:
        return false;
      case fir::InstrType::Intrinsic:
        switch ((fir::IntrinsicSubType)instr->subtype) {
          case fir::IntrinsicSubType::INVALID:
          case fir::IntrinsicSubType::CTLZ:
          case fir::IntrinsicSubType::Abs:
          case fir::IntrinsicSubType::FAbs:
          case fir::IntrinsicSubType::VA_end:
          case fir::IntrinsicSubType::VA_start:
          case fir::IntrinsicSubType::UMin:
          case fir::IntrinsicSubType::UMax:
          case fir::IntrinsicSubType::SMin:
          case fir::IntrinsicSubType::SMax:
          case fir::IntrinsicSubType::FMin:
          case fir::IntrinsicSubType::FMax:
          case fir::IntrinsicSubType::FRound:
          case fir::IntrinsicSubType::FFloor:
          case fir::IntrinsicSubType::PopCnt:
          case fir::IntrinsicSubType::FCeil:
          case fir::IntrinsicSubType::FTrunc:
          case fir::IntrinsicSubType::IsConstant:
            break;
        }
      case fir::InstrType::VectorInstr:
      case fir::InstrType::ICmp:
      case fir::InstrType::FCmp:
      case fir::InstrType::BinaryInstr:
      case fir::InstrType::UnaryInstr:
      case fir::InstrType::AllocaInstr:
      case fir::InstrType::ExtractValue:
      case fir::InstrType::InsertValue:
      case fir::InstrType::ITrunc:
      case fir::InstrType::ZExt:
      case fir::InstrType::SExt:
      case fir::InstrType::Conversion:
      case fir::InstrType::SelectInstr:
      case fir::InstrType::ReturnInstr:
      case fir::InstrType::BranchInstr:
      case fir::InstrType::CondBranchInstr:
      case fir::InstrType::SwitchInstr:
      case fir::InstrType::Unreachable:
      case fir::InstrType::LoadInstr:
      case fir::InstrType::StoreInstr:
      case fir::InstrType::AtomicRMW:
      case fir::InstrType::Fence:
        break;
    }
  }

  bool applied = false;
  for (auto pred : curr.pred) {
    auto pred_bb = cfg.bbrs[pred].bb;
    auto pred_term = pred_bb->get_terminator();
    if (pred_term->is(fir::InstrType::SwitchInstr)) {
      // TODO: impl
      continue;
    }
    if (pred_term->is(fir::InstrType::CondBranchInstr)) {
      fir::Builder bb(pred_bb);
      bb.at_end(pred_bb);

      u32 target_bb_arg_id = pred_term.get_bb_id(curr.bb);
      ASSERT(target_bb_arg_id == 0 || target_bb_arg_id == 1);
      u32 other_bb_arg_id = 1 - target_bb_arg_id;

      auto new_term = bb.build_branch(pred_term->bbs[other_bb_arg_id].bb);
      for (auto bb_arg : pred_term->bbs[other_bb_arg_id].args) {
        new_term.add_bb_arg(0, bb_arg);
      }
      pred_term.destroy();
      applied = true;
    } else if (pred_term->is(fir::InstrType::BranchInstr)) {
      fir::Builder bb(pred_bb);
      bb.at_end(pred_bb);
      bb.build_unreach();
      pred_term.destroy();
      applied = true;
    }
  }

  return applied;
}

SimplifyCFG::Res SimplifyCFG::distribute_return_unreach(CFG &cfg,
                                                        CFG::Node &curr) {
  if (curr.bb->n_instrs() == 1 &&
      (curr.bb->get_terminator()->is(fir::InstrType::ReturnInstr) ||
       curr.bb->get_terminator()->is(fir::InstrType::Unreachable))) {
    ZoneScopedN("distr ret unreach");
    const auto n_args = curr.bb->n_args();
    const auto return_instr = curr.bb->get_terminator();
    TMap<fir::ValueR, fir::ValueR> subs{};
    bool modified_any = false;
    // bool modified_all = true;
    // TVec<u32> deleted;
    for (auto pred_id : curr.pred) {
      auto &pred = cfg.bbrs[pred_id];
      auto prev_term = pred.bb->get_terminator();
      if (pred.succ.size() != 1) {
        // modified_all = false;
        continue;
      }
      modified_any = true;
      subs.clear();
      for (size_t bb_arg_id = 0; bb_arg_id < n_args; bb_arg_id++) {
        subs.insert({fir::ValueR{curr.bb->args[bb_arg_id]},
                     fir::ValueR{prev_term->bbs[0].args[bb_arg_id]}});
      }

      fir::Builder bb{pred.bb};
      bb.at_end(pred.bb);

      auto new_return = bb.insert_copy(return_instr);
      new_return.substitute(subs);
      prev_term.destroy();
    }
    // if (modified_all) {
    // TODO("okak");
    // }
    if (modified_any) {
      return Res::NeedUpdate;
    }
    return Res::NoChange;
  }
  return Res::NoChange;
}

SimplifyCFG::Res SimplifyCFG::merge_empty_block_backwards(CFG &cfg,
                                                          Dominators &dom,
                                                          CFG::Node &curr,
                                                          fir::Function &func,
                                                          size_t bb_id) {
  if (curr.succ.size() != 1 || curr.succ[0] == bb_id) {
    return Res::NoChange;
  }

  auto &succ = cfg.bbrs[curr.succ[0]];
  // TODO: this setup leads to infinite replcements
  //  0x5591b4d37910():
  //    0x5591b4db2948 : () = Branch<0x5591b4d379c0()>(){}
  //  0x5591b4d37968():
  //    0x5591b4d32ba8 : () = Branch<0x5591b4d379c0()>(){}
  //  0x5591b4d379c0():
  //    0x5591b4d32998 : () = Branch<0x5591b4d37968()>(){}

  // cant call itself
  for (auto sucsuc : succ.succ) {
    if (sucsuc == curr.succ[0]) {
      return Res::NoChange;
    }
  }

  if (succ.bb->n_instrs() != 1 || succ.bb->n_args() != 0) {
    return Res::NoChange;
  }
  ZoneScopedN("merge backw");
  auto old_term = func.basic_blocks[bb_id]->get_terminator();

  fir::Builder bb{curr.bb};
  bb.at_end(curr.bb);
  bb.insert_copy(succ.bb->get_terminator());
  old_term.remove_from_parent();
  if (succ.pred.size() == 1 && succ.bb != curr.bb) {
    succ.bb->remove_from_parent(true, true, true);
    (void)dom;
    // auto succ_id = curr.succ[0];
    // cfg.update_merge_succ(bb_id, succ_id);
    // cfg.update_delete(succ_id);
    // dom.update(cfg);
    return Res::NeedUpdate;
  }
  // cfg.update_delete_term(bb_id);
  // cfg.update_merge_succ(bb_id, curr.succ[0]);
  // dom.update(cfg);
  return Res::NeedUpdate;
}

bool SimplifyCFG::merge_empty_block_forwards(CFG &cfg, CFG::Node &curr,
                                             fir::Function &func, size_t bb_id,
                                             bool is_entry) {
  if (curr.bb->n_instrs() == 1 &&
      curr.bb->get_terminator()->is(fir::InstrType::BranchInstr)) {
    ZoneScopedN("merge forw");
    ASSERT(curr.succ.size() == 1);
    auto succ = cfg.bbrs[curr.succ[0]].bb;
    if (curr.succ[0] == bb_id) {
      return false;
    }
    // if no bb args involved just replace
    if (curr.bb->n_args() == 0 && succ->n_args() == 0 &&
        (!is_entry ||
         (cfg.bbrs[curr.succ[0]].pred.size() == 1 && curr.succ[0] == 1))) {
      curr.bb->replace_all_uses(fir::ValueR(succ));
      curr.bb->remove_from_parent(true, true, true);
      return true;
    }
    // if this block doesnt have bb_args we might be able to replace all
    // incoming edges with the outgoig jump + args
    // TODO: handle entry
    if (curr.bb->n_args() == 0 && !is_entry) {
      const auto &terminator_args = curr.bb->get_terminator()->bbs[0].args;
      TVec<fir::Use> uses(curr.bb->get_uses().begin(),
                          curr.bb->get_uses().end());
      for (const auto &use : uses) {
        ASSERT(use.type == fir::UseType::BB);
        auto bb_id = use.argId;
        auto user = use.user;
        ASSERT(user->bbs[bb_id].args.size() == 0);
        user.replace_bb(bb_id, succ, false);

        // ASSERT(user->bbs[bb_id].args.size() == 0);
        for (auto old_arg : terminator_args) {
          user.add_bb_arg(bb_id, old_arg);
        }
        ASSERT(user->bbs[bb_id].args.size() == terminator_args.size());
      }
      ASSERT(curr.bb->get_n_uses() == 0);
      func.basic_blocks[bb_id]->remove_from_parent(true, true, true);
      return true;
    }
  }
  return false;
}

SimplifyCFG::Res SimplifyCFG::merge_linear_relation(CFG &cfg, Dominators &dom,
                                                    CFG::Node &curr,
                                                    fir::Function &func,
                                                    size_t bb_id) {
  if (curr.succ.size() != 1 || bb_id == curr.succ[0]) {
    return Res::NoChange;
  }
  auto succ_id = curr.succ[0];
  u32 secon_n_args = func.basic_blocks.at(succ_id)->n_args();
  auto old_first_term = func.basic_blocks[bb_id]->get_terminator();
  if (!old_first_term->is(fir::InstrType::BranchInstr)) {
    return Res::NoChange;
  }

  if (cfg.bbrs[curr.succ[0]].pred.size() == 1) {
    ZoneScopedN("merge linrel basic");
    bool secon_has_args = secon_n_args != 0;

    if (secon_has_args) {
      auto succ = func.basic_blocks[succ_id];
      for (u32 i = 0; i < succ->n_args(); i++) {
        succ->args[i]->replace_all_uses(old_first_term->bbs[0].args[i]);
      }
      succ->clear_args();
    }

    fir::Builder bb{curr.bb};
    bb.at_end(curr.bb);

    for (size_t instr_id = 0;
         !func.basic_blocks[succ_id]->instructions.empty();) {
      auto &instr = func.basic_blocks[succ_id]->instructions[instr_id];
      bb.move_instr(instr);
    }

    old_first_term.remove_from_parent();
    func.basic_blocks[succ_id]->remove_from_parent(true, true, true);
    (void)dom;
    // cfg.update_merge_succ(bb_id, succ_id);
    // cfg.update_delete(succ_id);
    // dom.update(cfg);
    return Res::NeedUpdate;
  }
  if (secon_n_args * 1UL + 1 >=
      func.basic_blocks[succ_id]->instructions.size()) {
    ZoneScopedN("merge linrel adv");
    // TODO this can be improved for now
    //  we just check that all the bbargs and values inside the bb are *only*
    //  used within this bb to not destroy any indirect usages when copying the
    //  instruction

    fir::BasicBlock old_bb = func.basic_blocks[succ_id];
    for (auto arg : old_bb->args) {
      for (auto use : arg->uses) {
        if (use.user->get_parent() != old_bb) {
          return Res::NoChange;
        }
      }
    }
    for (auto instr : old_bb->instructions) {
      for (auto use : instr->uses) {
        if (use.user->get_parent() != old_bb) {
          return Res::NoChange;
        }
      }
    }
    // fmt::println("==============================\n{:cd}",
    //              *curr.bb->get_parent().func);
    // fmt::println("{:cd}", old_bb);
    fir::ContextData::V2VMap subs;
    for (u32 i = 0; i < old_bb->n_args(); i++) {
      subs.insert({fir::ValueR{old_bb->args[i]},
                   fir::ValueR{old_first_term->bbs[0].args[i]}});
    }
    fir::Builder bb{curr.bb};
    bb.at_end(curr.bb);

    for (auto instr : old_bb->instructions) {
      auto new_instr = bb.insert_copy(instr);
      new_instr.substitute(subs);
      subs.insert({fir::ValueR{instr}, fir::ValueR{new_instr}});
    }
    old_first_term.destroy();
    // fmt::println("{:cd}\n+++++++++++++++++++++++++++++++++++++++",
    //              *curr.bb->get_parent().func);
    // fmt::println("applied");
    utils::StatCollector::get().addi(1, "MergedShort+ManyArgsIntoTerm");
    return Res::NeedUpdate;
  }
  return Res::NoChange;
}

bool SimplifyCFG::conditional_to_cmove(CFG::Node &curr) {
  auto terminator = curr.bb->get_terminator();
  if (curr.succ.size() == 2 &&
      terminator->is(fir::InstrType::CondBranchInstr) &&
      terminator->bbs[0].args.empty() && terminator->bbs[1].args.empty()) {
    ZoneScopedN("cond to cmove1");
    auto t1 = terminator->bbs[0].bb;
    auto t2 = terminator->bbs[1].bb;
    bool matched = false;
    fir::Instr extra_instr;
    u8 extra_bb = 0;
    // TODO: prob should ignore instrucitons that are part of both so get
    // executed either way? but technically that should already be taken care by
    // redundancy eliminiation or PRE
    if (t1->n_instrs() == 1 &&
        t1->instructions[0]->is(fir::InstrType::ReturnInstr) &&
        t2->n_instrs() == 1 &&
        t2->instructions[0]->is(fir::InstrType::ReturnInstr) &&
        // need a return value
        t2->instructions[0]->args.size() == 1) {
      matched = true;
    }
    if (t1->n_instrs() == 1 &&
        t1->instructions[0]->is(fir::InstrType::ReturnInstr) &&
        t2->n_instrs() == 2 &&
        t2->instructions[1]->is(fir::InstrType::ReturnInstr) &&
        t2->instructions[1]->args.size() == 1 &&
        t2->instructions[1]->args[0] == fir::ValueR{t2->instructions[0]}) {
      extra_instr = t2->instructions[0];
      matched = true;
      extra_bb = 1;
    }
    if (t2->n_instrs() == 1 &&
        t2->instructions[0]->is(fir::InstrType::ReturnInstr) &&
        t1->n_instrs() == 2 &&
        t1->instructions[1]->is(fir::InstrType::ReturnInstr) &&
        t1->instructions[1]->args.size() == 1 &&
        t1->instructions[1]->args[0] == fir::ValueR{t1->instructions[0]}) {
      extra_instr = t1->instructions[0];
      matched = true;
      extra_bb = 0;
    }
    if (matched) {
      fir::Builder bb{terminator};
      auto res_t = t1->get_terminator()->get_type();
      // fmt::println("{:cd}", *t1->get_parent().func);
      ASSERT(!res_t->is_void());
      fir::ValueR v1;
      fir::ValueR v2;
      if (!extra_instr.is_valid()) {
        v1 = t1->get_terminator()->args[0];
        v2 = t2->get_terminator()->args[0];
      } else if (extra_bb == 1) {
        v1 = t1->get_terminator()->args[0];
        v2 = fir::ValueR{bb.insert_copy(extra_instr)};
      } else if (extra_bb == 0) {
        v1 = fir::ValueR{bb.insert_copy(extra_instr)};
        v2 = t2->get_terminator()->args[0];
      }
      auto res_v = bb.build_select(res_t, terminator->args[0], v1, v2);
      bb.build_return(res_v);
      terminator.destroy();
      // fmt::println("{:cd}", curr.bb);
      // TODO("okak");
      return true;
    }
  }
  if (curr.succ.size() == 2 && terminator->bbs[0].bb == terminator->bbs[1].bb) {
    ZoneScopedN("cond to cmove2");

    auto &args1 = terminator->bbs[0].args;
    auto &args2 = terminator->bbs[1].args;
    auto condition = terminator->get_arg(0);
    auto target = terminator->bbs[0].bb;

    // TODO: prob needs some heursitics if its worth it
    fir::Builder bb{curr.bb};
    bb.at_penultimate(curr.bb);
    TVec<fir::ValueR> new_inputs;
    for (size_t i = 0; i < args1.size(); i++) {
      new_inputs.push_back(
          bb.build_select(args1[i].get_type(), condition, args1[i], args2[i]));
    }
    auto new_branch = bb.build_branch(target);
    for (auto &input : new_inputs) {
      new_branch.add_bb_arg(0, input);
    }
    terminator.remove_from_parent();
    return true;
  }
  return false;
}

SimplifyCFG::Res SimplifyCFG::eliminate_infinite_loop(CFG &cfg, Dominators &dom,
                                                      CFG::Node &curr,
                                                      size_t bb_id,
                                                      fir::Function &func) {
  if (!func.attribs.must_progress) {
    return Res::NoChange;
  }
  auto terminator = curr.bb->get_terminator();
  // TODO: implement more complex infinite loop detection
  if (terminator->is(fir::InstrType::BranchInstr) &&
      terminator->bbs[0].bb == curr.bb) {
    ZoneScopedN("elim infinite loop");
    fir::Builder bb{curr.bb};
    bb.at_penultimate(curr.bb);
    bb.build_unreach();
    terminator.destroy();

    cfg.update_delete_term(bb_id);
    dom.update(cfg);
    return Res::Changed;
  }
  return Res::NoChange;
}

namespace {
bool can_merge(const fir::BasicBlock bb1, u32 next_id, bool &sec_neg,
               bool &can_rename, bool &needs_rename, const CFG &cfg) {
  fir::BasicBlock bb2 = bb1->get_terminator()->bbs[next_id].bb;
  auto &exp_other = bb1->get_terminator()->bbs[1 - next_id];
  auto t2 = bb2->get_terminator();
  needs_rename = false;
  // TODO: either we are the only one calling the bb then we can do sbustitution
  // or they are
  //  only used within the same bb then we can just substitue our new
  //  instructions
  can_rename = cfg.bbrs[cfg.get_bb_id(bb2)].pred.size() == 1;
  if (bb2 == bb1) {
    return false;
  }
  if (!t2->is(fir::InstrType::CondBranchInstr)) {
    return false;
  }
  if (t2->bbs[0].bb == exp_other.bb) {
    for (size_t bb_arg_id = 0; bb_arg_id < t2->bbs[0].args.size();
         bb_arg_id++) {
      if (t2->bbs[0].args[bb_arg_id] != exp_other.args[bb_arg_id]) {
        return false;
      }
    }
    sec_neg = next_id != 1;
  } else if (t2->bbs[1].bb == exp_other.bb) {
    for (size_t bb_arg_id = 0; bb_arg_id < t2->bbs[1].args.size();
         bb_arg_id++) {
      if (t2->bbs[1].args[bb_arg_id] != exp_other.args[bb_arg_id]) {
        return false;
      }
    }
    sec_neg = next_id != 0;
  } else {
    return false;
  }
  if (bb2->instructions.size() > 5) {
    return false;
  }
  for (auto i : bb2->args) {
    for (auto u : i->get_uses()) {
      if (u.user->parent != bb2) {
        if (!can_rename) {
          return false;
        }
        needs_rename = true;
      }
    }
  }
  for (auto i : bb2->instructions) {
    for (auto u : i->get_uses()) {
      if (u.user->parent != bb2) {
        if (!can_rename) {
          return false;
        }
        needs_rename = true;
      }
    }
    if (i->has_pot_sideeffects() || i->pot_modifies_mem() ||
        i->pot_reads_mem()) {
      return false;
    }
  }
  return true;
}
}  // namespace

bool SimplifyCFG::merge_term_cond(CFG &cfg, CFG::Node &curr) {
  auto terminator = curr.bb->get_terminator();
  if (!terminator->is(fir::InstrType::CondBranchInstr)) {
    return false;
  }
  u32 merge_num = 0;
  bool sec_negated = false;
  // rename meaning that it only has this incoming edge
  //  and some of the values in the 2nd block are used afterwards
  //  then we can ofcourse just replace them with our newly copied versions in
  //  bb1
  bool can_rename = false;
  bool needs_rename = false;

  fir::BasicBlock merge_target{fir::BasicBlock::invalid()};
  // TODO: would need to check if they are used after
  ZoneScopedN("merge term cond");
  if (can_merge(curr.bb, 0, sec_negated, can_rename, needs_rename, cfg)) {
    merge_target = terminator->bbs[0].bb;
    merge_num = 0;
  } else if (can_merge(curr.bb, 1, sec_negated, can_rename, needs_rename,
                       cfg)) {
    merge_target = terminator->bbs[1].bb;
    merge_num = 1;
  } else {
    return false;
  }

  if (!merge_target->args.empty() && !can_rename) {
    return false;
  }

  // fmt::println("{:cd}", curr.bb);
  // fmt::println("{:cd}", merge_target);
  auto term2 = merge_target->get_terminator();
  fir::Builder buh{terminator};
  TMap<fir::ValueR, fir::ValueR> subs;
  // gotta substitute bbargs if there are any
  if (!merge_target->args.empty()) {
    ASSERT(can_rename);
    ASSERT(needs_rename);

    for (size_t arg_id = 0; arg_id < merge_target->args.size(); arg_id++) {
      subs.insert({fir::ValueR{merge_target->args[arg_id]},
                   fir::ValueR{terminator->bbs[merge_num].args[arg_id]}});
    }
    // fmt::println("{:cd}", terminator->get_parent());
    // fmt::println("{:cd}", term2->get_parent());
    // fmt::println("{} {}", can_rename, needs_rename);
    // TODO("fail fixme");
  }
  for (size_t merge_i = 0; merge_i < merge_target->instructions.size() - 1;
       merge_i++) {
    auto n_instr = buh.insert_copy(merge_target->instructions[merge_i]);
    subs.insert({fir::ValueR{merge_target->instructions[merge_i]},
                 fir::ValueR{n_instr}});
    n_instr.substitute(subs);
  }
  auto &out_target =
      sec_negated ? term2->bbs[1 - merge_num] : term2->bbs[merge_num];
  terminator.replace_bb(merge_num, out_target.bb, false, false);
  for (auto new_arg : out_target.args) {
    if (subs.contains(new_arg)) {
      terminator.add_bb_arg(merge_num, subs.at(new_arg));
    } else {
      terminator.add_bb_arg(merge_num, new_arg);
    }
  }
  auto new_cond = term2->args[0];
  if (subs.contains(term2->args[0])) {
    new_cond = subs.at(term2->args[0]);
  }
  if (merge_num == 1) {
    if (sec_negated) {
      new_cond = buh.build_unary_op(new_cond, fir::UnaryInstrSubType::Not);
    }
    auto new_res_cond = buh.build_binary_op(terminator->args[0], new_cond,
                                            fir::BinaryInstrSubType::Or);
    terminator.replace_arg(0, new_res_cond);
  } else if (merge_num == 0) {
    auto c1 = terminator->args[0];
    auto c2 = new_cond;
    if (sec_negated) {
      c2 = buh.build_unary_op(c2, fir::UnaryInstrSubType::Not);
    }
    auto new_res_cond =
        buh.build_binary_op(c1, c2, fir::BinaryInstrSubType::And);
    terminator.replace_arg(0, new_res_cond);
  }

  if (needs_rename) {
    TVec<fir::Use> to_repl_uses;
    for (auto arg : merge_target->args) {
      to_repl_uses.clear();
      for (auto use : arg->get_uses()) {
        if (use.user->get_parent() != merge_target) {
          to_repl_uses.push_back(use);
        }
      }
      if (!to_repl_uses.empty()) {
        auto sub = subs.at(fir::ValueR{arg});
        for (auto use : to_repl_uses) {
          use.replace_use(sub);
        }
      }
    }
    for (auto instr : merge_target->instructions) {
      if (!instr->has_result()) {
        continue;
      }
      to_repl_uses.clear();
      for (auto use : instr->get_uses()) {
        if (use.user->get_parent() != merge_target) {
          to_repl_uses.push_back(use);
        }
      }
      if (!to_repl_uses.empty()) {
        auto sub = subs.at(fir::ValueR{instr});
        for (auto use : to_repl_uses) {
          use.replace_use(sub);
        }
      }
    }
  }
  return true;
}

bool SimplifyCFG::backpull_term_cond(CFG &cfg, CFG::Node &curr,
                                     Dominators &dom) {
  (void)dom;
  (void)cfg;
  // auto curr_id = cfg.get_bb_id(curr.bb);
  auto term1 = curr.bb->get_terminator();
  if (!term1->is(fir::InstrType::CondBranchInstr)) {
    return false;
  }
  for (u16 ti = 0; ti < 2; ti++) {
    auto target = term1->bbs[ti].bb;
    if (target->n_instrs() != 1 || target == curr.bb) {
      continue;
    }
    auto term2 = target->get_terminator();
    if (!term2->is(fir::InstrType::CondBranchInstr)) {
      continue;
    }
    if (term2->bbs[ti].bb == target) {
      continue;
    }
    if (term1->args[0] != term2->args[0]) {
      continue;
    }
    // so cant do the backwarding iff the arguments of the target block are used
    // after the target block. Since then it would be uninitialized
    //
    // It only matters tho if we actually can reach this code from the branch we
    // will pull backwards
    if (!target->args.empty()) {
      bool any_args_used_later = false;
      auto next_bb = cfg.get_bb_id(term2->bbs[ti].bb);
      utils::BitSet reachable_bbs(cfg.bbrs.size(), false);
      reachable_bbs[next_bb].set(true);
      TVec<u32> worklist;
      worklist.push_back(next_bb);
      while (!worklist.empty()) {
        u32 c = worklist.back();
        worklist.pop_back();
        for (auto n : cfg.bbrs[c].succ) {
          if (!reachable_bbs[n]) {
            reachable_bbs[c].set(true);
            worklist.push_back(n);
          }
        }
      }

      for (auto arg : target->args) {
        for (auto usi : arg->get_uses()) {
          auto user_bb = usi.user->get_parent();
          if (user_bb != target && reachable_bbs[cfg.get_bb_id(user_bb)]) {
            any_args_used_later = true;
            break;
          }
        }
      }
      if (any_args_used_later) {
        continue;
      }
    }
    term1.replace_bb(ti, term2->bbs[ti].bb, false, false);
    for (auto arg : term2->bbs[ti].args) {
      // need to substitute bb args
      if (arg.is_bb_arg()) {
        auto a = arg.as_bb_arg();
        if (a->_parent == target) {
          term1.add_bb_arg(ti, term1->bbs[ti].args[target->get_arg_id(a)]);
        } else {
          term1.add_bb_arg(ti, arg);
        }
      } else {
        term1.add_bb_arg(ti, arg);
      }
    }
    return true;
  }

  return false;
}

SimplifyCFG::Res SimplifyCFG::simplify_bb_args(CFG &cfg, Dominators &dom,
                                               fir::Function &func,
                                               size_t bb_id) {
  (void)dom;
  auto &curr = cfg.bbrs[bb_id];
  bool is_entry = bb_id == cfg.entry;
  if (remove_dead_bb_arg(curr, func, is_entry)) {
    if constexpr (debug_print) {
      fmt::println("2");
    }
    return Res::Changed;
  }
  if (remove_dup_bb_args(curr, is_entry)) {
    if constexpr (debug_print) {
      fmt::println("3");
    }
    return Res::Changed;
  }
  // if (remove_constant_bb_args(curr, is_entry)) {
  //   if constexpr (debug_print) {
  //     fmt::println("4");
  //   }
  //   return Res::Changed;
  // }
  if (remove_useless_bb_args(cfg, curr)) {
    if constexpr (debug_print) {
      fmt::println("5");
    }
    return Res::Changed;
  }
  auto r = remove_struct_bb_arg(cfg, curr);
  if (r != Res::NoChange) {
    if constexpr (debug_print) {
      fmt::println("6");
    }
    return r;
  }
  return Res::NoChange;
}

SimplifyCFG::Res SimplifyCFG::simplify_cfg(CFG &cfg, Dominators &dom,
                                           fir::Function &func, size_t bb_id) {
  auto &curr = cfg.bbrs[bb_id];
  bool is_entry = bb_id == cfg.entry;

  auto r = remove_dead_bb(cfg, dom, curr, func, bb_id, is_entry);
  if (r != Res::NoChange) {
    if constexpr (debug_print) {
      fmt::println("1");
    }
    return r;
  }

  r = merge_linear_relation(cfg, dom, curr, func, bb_id);
  if (r != Res::NoChange) {
    if constexpr (debug_print) {
      fmt::println("15");
    }
    return r;
  }

  r = merge_empty_block_backwards(cfg, dom, curr, func, bb_id);
  if (r != Res::NoChange) {
    if constexpr (debug_print) {
      fmt::println("7");
    }
    return r;
  }

  if (merge_empty_block_forwards(cfg, curr, func, bb_id, is_entry)) {
    if constexpr (debug_print) {
      fmt::println("8");
    }
    return Res::NeedUpdate;
  }

  r = distribute_return_unreach(cfg, curr);
  if (r != Res::NoChange) {
    if constexpr (debug_print) {
      fmt::println("9");
    }
    return r;
  }

  if (remove_unreach(cfg, curr, is_entry)) {
    if constexpr (debug_print) {
      fmt::println("10");
    }
    return Res::NeedUpdate;
  }

  if (conditional_to_cmove(curr)) {
    if constexpr (debug_print) {
      fmt::println("11");
    }
    return Res::NeedUpdate;
  }

  r = eliminate_infinite_loop(cfg, dom, curr, bb_id, func);
  if (r != Res::NoChange) {
    if constexpr (debug_print) {
      fmt::println("12");
    }
    return r;
  }

  if (merge_term_cond(cfg, curr)) {
    if constexpr (debug_print) {
      fmt::println("13");
    }
    return Res::NeedUpdate;
  }

  if (backpull_term_cond(cfg, curr, dom)) {
    if constexpr (debug_print) {
      fmt::println("14");
    }
    return Res::NeedUpdate;
  }

  r = flip_cold_cond(cfg, curr);
  if (r != Res::NoChange) {
    if constexpr (debug_print) {
      fmt::println("15");
    }
    return r;
  }

  r = static_select_call_into_branch(func, curr);
  if (r != Res::NoChange) {
    if constexpr (debug_print) {
      fmt::println("16");
    }
    return r;
  }

  if constexpr (debug_print) {
    fmt::println("nuffin");
  }
  return Res::NoChange;
}

void SimplifyCFG::apply(fir::Context &_, fir::Function &func) {
  ZoneScopedNC("SimplifyCFG", COLOR_OPTIMF);
  // Cant really simplify the cfg if theres just 1 node
  if (func.basic_blocks.size() == 1) {
    return;
  }
  CFG cfg{func};
  Dominators dom{cfg};

  auto iter = 0;
  bool modified = true;
  bool needs_update = true;
  while (modified) {
    modified = false;
    needs_update = false;
    for (size_t bb_id = 1; bb_id <= cfg.bbrs.size(); bb_id++) {
      auto r1 = simplify_bb_args(cfg, dom, func, bb_id - 1);
      modified |= (r1 == Res::Changed || r1 == Res::NeedUpdate);
      if (r1 == Res::NeedUpdate) {
        needs_update = true;
        break;
      }
    }
    if (!needs_update) {
      for (size_t bb_id = 1; bb_id <= cfg.bbrs.size(); bb_id++) {
        auto r2 = simplify_cfg(cfg, dom, func, bb_id - 1);
        modified |= (r2 == Res::Changed || r2 == Res::NeedUpdate);
        if (r2 == Res::NeedUpdate) {
          needs_update = true;
          break;
        }
      }
    }
    if (iter++ > 100) {
      failure(
          {.reason = "Didnt converge fixme\n", .loc = func.basic_blocks[0]});
      break;
    }
    if (!modified) {
      break;
    }
    if (needs_update) {
      cfg = {};
      dom = {};
      foptim::utils::TempAlloc<void *>::reset();
      cfg = CFG(func, false);
      dom = Dominators(cfg);
    }
  }
  dup_bb_to_args(func, cfg);
}

}  // namespace foptim::optim
