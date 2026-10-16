#include "loop_unroll.hpp"

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {

void peel_it(CFG &cfg, LoopInfo &loop, u8 peel_factor, fir::Context &ctx,
             fir::Function &func, bool known_to_iterate_more) {
  i64 head_id = -1;
  for (u64 i = 0; i < loop.body_nodes.size(); i++) {
    if (loop.body_nodes[i] == loop.head) {
      head_id = static_cast<i64>(i);
    }
  }
  ASSERT(head_id >= 0);
  ASSERT(peel_factor <= 16);
  TVec<fir::BasicBlock> new_bbs[16];
  fir::ContextData::V2VMap subs;

  // for each peel we copy the body
  for (u8 u = 0; u < peel_factor; u++) {
    subs.clear();
    for (auto node : loop.body_nodes) {
      auto new_bb = ctx->copy(cfg.bbrs[node].bb, subs, false);
      new_bbs[u].push_back(new_bb);
    }
    for (auto &bb : new_bbs[u]) {
      for (auto instr : bb->instructions) {
        instr.substitute(subs);
      }
    }
  }

  for (u8 u = 0; u < peel_factor - 1; u++) {
    // then update the backwards edges and insert our new bbs
    subs.clear();
    auto first_bb = new_bbs[u][head_id];
    auto sec_bb = new_bbs[u + 1][head_id];
    subs.insert({fir::ValueR(first_bb), fir::ValueR(sec_bb)});
    for (auto bb : new_bbs[u]) {
      for (auto instr : bb->instructions) {
        instr.substitute(subs);
      }
      func.append_bbr(bb);
    }
    if (known_to_iterate_more) {
      auto old_term = first_bb->get_terminator();
      auto bb = fir::Builder(first_bb);
      bb.at_end(first_bb);
      auto new_branch = bb.build_branch(sec_bb);
      auto id = old_term.get_bb_id(sec_bb);
      for (auto arg : old_term->bbs[id].args) {
        new_branch.add_bb_arg(0, arg);
      }
      old_term.remove_from_parent();
    }
    // TODO("okak");
  }

  // update the last one + the original
  {
    // then update the backwards edges and insert our new bbs
    subs.clear();
    auto first_bb = new_bbs[peel_factor - 1][head_id];
    auto sec_bb = cfg.bbrs[loop.body_nodes[head_id]].bb;

    subs.insert({fir::ValueR(first_bb), fir::ValueR(sec_bb)});
    for (auto bb : new_bbs[peel_factor - 1]) {
      for (auto instr : bb->instructions) {
        instr.substitute(subs);
      }
      func.append_bbr(bb);
    }

    if (known_to_iterate_more) {
      auto old_term = first_bb->get_terminator();
      auto bb = fir::Builder(first_bb);
      bb.at_end(first_bb);
      auto new_branch = bb.build_branch(sec_bb);
      auto id = old_term.get_bb_id(sec_bb);
      for (auto arg : old_term->bbs[id].args) {
        new_branch.add_bb_arg(0, arg);
      }
      old_term.remove_from_parent();
    }

    // and also update the original incoming edges (but not the original
    // backwards edges)
    subs.clear();
    subs.insert({fir::ValueR(cfg.bbrs[loop.body_nodes[head_id]].bb),
                 fir::ValueR(new_bbs[0][head_id])});
    for (auto bb_id : cfg.bbrs[loop.head].pred) {
      if (std::ranges::contains(loop.body_nodes, bb_id)) {
        continue;
      }
      for (auto instr : cfg.bbrs[bb_id].bb->instructions) {
        instr.substitute(subs);
      }
    }
  }
}

void unroll_it(CFG &cfg, LoopInfo &loop, u8 unroll_factor, fir::Context &ctx,
               fir::Function &func, bool is_full_unroll) {
  // since the original alraedy exists we substract one of the unroll factor
  unroll_factor -= 1;

  // get which id in the new_bbs as wel as in the body notes matches the head
  // this then we use to redirect the backwards edges to the next part of the
  // loop
  i64 head_id = -1;
  for (u64 i = 0; i < loop.body_nodes.size(); i++) {
    if (loop.body_nodes[i] == loop.head) {
      head_id = static_cast<i64>(i);
    }
  }
  ASSERT(head_id >= 0);

  ASSERT(unroll_factor <= 32);
  TVec<fir::BasicBlock> new_bbs[32];
  fir::ContextData::V2VMap subs;

  // generate new bbs
  // for each unroll we copy the body
  for (u8 u = 0; u < unroll_factor; u++) {
    subs.clear();
    for (auto node : loop.body_nodes) {
      auto new_bb = ctx->copy(cfg.bbrs[node].bb, subs, false);
      new_bbs[u].push_back(new_bb);
    }
    // then update the newly generated bbs
    for (auto &bb : new_bbs[u]) {
      for (auto instr : bb->instructions) {
        instr.substitute(subs);
      }
    }
  }

  for (u8 u = 0; u < unroll_factor - 1; u++) {
    // then update the backwards edges and insert our new bbs
    subs.clear();
    auto first_bb = new_bbs[u][head_id];
    auto sec_bb = new_bbs[u + 1][head_id];
    subs.insert({fir::ValueR(first_bb), fir::ValueR(sec_bb)});
    for (auto bb : new_bbs[u]) {
      for (auto instr : bb->instructions) {
        instr.substitute(subs);
      }
      func.append_bbr(bb);
    }
    auto old_term = first_bb->get_terminator();
    auto bb = fir::Builder(first_bb);
    bb.at_end(first_bb);
    auto new_branch = bb.build_branch(sec_bb);
    auto id = old_term.get_bb_id(sec_bb);
    for (auto arg : old_term->bbs[id].args) {
      new_branch.add_bb_arg(0, arg);
    }
    old_term.remove_from_parent();
    // TODO("okak");
  }

  // update the last one + the original
  {
    // then update the backwards edges and insert our new bbs
    subs.clear();
    auto first_bb = new_bbs[unroll_factor - 1][head_id];
    auto sec_bb = cfg.bbrs[loop.body_nodes[head_id]].bb;
    subs.insert({fir::ValueR(first_bb), fir::ValueR(sec_bb)});
    for (auto bb : new_bbs[unroll_factor - 1]) {
      for (auto instr : bb->instructions) {
        instr.substitute(subs);
      }
      func.append_bbr(bb);
    }

    auto old_term = first_bb->get_terminator();
    auto bb = fir::Builder(first_bb);
    bb.at_end(first_bb);
    auto new_branch = bb.build_branch(sec_bb);
    auto id = old_term.get_bb_id(sec_bb);
    for (auto arg : old_term->bbs[id].args) {
      new_branch.add_bb_arg(0, arg);
    }
    old_term.remove_from_parent();

    // and also update the  backwards edges in the original
    subs.clear();
    subs.insert({fir::ValueR(cfg.bbrs[loop.body_nodes[head_id]].bb),
                 fir::ValueR(new_bbs[0][head_id])});
    for (auto &bb : cfg.bbrs) {
      for (auto instr : bb.bb->instructions) {
        instr.substitute(subs);
      }
    }

    if (is_full_unroll) {
      auto orig_bb = cfg.bbrs[loop.body_nodes[head_id]].bb;
      auto term = orig_bb->get_terminator();
      ASSERT(term->is(fir::InstrType::CondBranchInstr));

      auto bb = fir::Builder(orig_bb);
      bb.at_end(orig_bb);
      auto id = 1 - term.get_bb_id(new_bbs[0][head_id]);
      auto new_branch = bb.build_branch(term->bbs[id].bb);
      for (auto arg : term->bbs[id].args) {
        new_branch.add_bb_arg(0, arg);
      }
      term.remove_from_parent();
      // fmt::println("======================");
      // fmt::println("{}", orig_bb);
    }
  }
  // fmt::println("{}", func);
  // TODO("okka");
}

namespace {
/*IF we have a i==0 condition it might make sense to peel first iteration since
 * this now elimenates the else part in the peeled part and the if guarded part
 * inside the loop*/
bool peel_condition(CFG &cfg, LoopInfo &loop, fir::Context &ctx,
                    fir::Function &func, ScalarEvo &evo) {
  if (loop.body_nodes.size() < 2) {
    return false;
  }
  auto peel_count = 0;
  bool can_delete_cond = true;
  for (auto x : loop.body_nodes) {
    bool found = false;
    for (auto y : loop.tails) {
      if (x == y) {
        found = true;
        break;
      }
    }
    if (!found) {
      for (auto y : loop.leaving_nodes) {
        if (x == y) {
          found = true;
          break;
        }
      }
    }
    if (found) {
      continue;
    }
    auto term = cfg.bbrs[x].bb->get_terminator();
    if (!term->is(fir::InstrType::CondBranchInstr)) {
      continue;
    }
    if (!term->args[0].is_instr()) {
      continue;
    }
    auto cond = term->args[0].as_instr();
    if (!cond->is(fir::InstrType::ICmp)) {
      continue;
    }
    if (!cond->args[0].is_bb_arg()) {
      continue;
    }
    auto evo_var = cond->args[0].as_bb_arg();
    if (evo_var->get_parent() != cfg.bbrs[loop.head].bb) {
      continue;
    }
    u32 evo_id = evo_var->get_parent()->get_arg_id(evo_var);
    if (!evo.is_addrec(evo.get(fir::ValueR{evo_var}))) {
      continue;
    }

    if (cond->is(fir::ICmpInstrSubType::EQ)) {
      auto cond_target = cond->args[1];
      if (cfg.bbrs[loop.head].pred.size() != 1 + loop.tails.size()) {
        continue;
      }
      auto pred_term = fir::Instr(fir::Instr::invalid());
      for (auto pred : cfg.bbrs[loop.head].pred) {
        if (!std::ranges::contains(loop.tails, pred)) {
          pred_term = cfg.bbrs[pred].bb->get_terminator();
          break;
        }
      }
      bool found = false;
      for (const auto &pred_bb_target : pred_term->bbs) {
        if (pred_bb_target.bb == cfg.bbrs[loop.head].bb &&
            pred_bb_target.args[evo_id].eql(cond_target)) {
          found = true;
          break;
        }
      }
      if (!found) {
        continue;
      }
      peel_count = std::max(peel_count, 1);
      can_delete_cond = false;
    }
  }
  if (peel_count == 0) {
    return false;
  }

  if (can_delete_cond) {
    TODO("implement deleting the cond inside the loop?");
  }

  // loop.dump();
  // fmt::println("Should prob peel {} X times", peel_count);
  ASSERT(peel_count == 1);
  peel_it(cfg, loop, 1, ctx, func, false);
  // fmt::println("{:cd}", func);

  (void)cfg;
  (void)loop;
  (void)ctx;
  (void)func;
  return false;
}
} // namespace

bool LoopUnroll::apply_it(CFG &cfg, LoopInfo &loop, fir::Context &ctx,
                          fir::Function &func, TripCountAnalysis &tc) {
  (void)loop;
  (void)ctx;
  (void)func;
  size_t n_instrs = 0;
  bool has_call = false;
  for (auto i : loop.body_nodes) {
    n_instrs += cfg.bbrs[i].bb->n_instrs();
    for (auto instr : cfg.bbrs[i].bb->instructions) {
      has_call |= instr->is(fir::InstrType::CallInstr);
    }
  }
  // the call may be inlined later, afterwards the loop is a vectorizer
  // candidate, which an unrolled body is not
  if (has_call) {
    return false;
  }

  // number of times the body runs
  const i128 iteration_count_full = *tc.const_exit_iter + 1;
  if (iteration_count_full > (static_cast<i128>(1) << 62)) {
    return false;
  }
  auto iteration_count = static_cast<u64>(iteration_count_full);
  u8 unroll_factor = 2;
  bool is_full_unroll = false;
  bool did_peel = false;
  const bool small_full = iteration_count > 1 &&
                          iteration_count <= config.max_full_unroll &&
                          iteration_count * n_instrs <= config.max_full_instr;
  if (!small_full && iteration_count > 2 && ((iteration_count - 1) % 2) == 0) {
    peel_it(cfg, loop, 1, ctx, func, true);
    did_peel = true;
    iteration_count--;
    cfg.update(func, false);
  }
  // fmt::println("{:cd}", func);
  // fmt::println("{}", iteration_count);
  if (small_full) {
    is_full_unroll = true;
    unroll_factor = static_cast<u8>(iteration_count);
  } else if (iteration_count % 32 == 0) {
    unroll_factor = 32;
  } else if (iteration_count % 16 == 0) {
    unroll_factor = 16;
  } else if (iteration_count % 8 == 0) {
    unroll_factor = 8;
  } else if (iteration_count % 4 == 0) {
    unroll_factor = 4;
  } else {
    return did_peel;
  }

  if (unroll_factor > config.max_unroll) {
    unroll_factor = config.max_unroll;
    is_full_unroll = false;
  }

  while (!is_full_unroll && unroll_factor * n_instrs > config.max_instr &&
         unroll_factor > 1) {
    unroll_factor = unroll_factor / 2;
  }
  if (unroll_factor == 1) {
    failure(
        {.reason = "Unroll prob too massive", .loc = {cfg.bbrs[loop.head].bb}});
    return did_peel;
  }
  // fmt::println("{}", unroll_factor);
  // fmt::println("unrolled {} {}", n_instrs, unroll_factor);

  // fmt::println("{}", func);
  // fmt::println("unrollnig!!");
  // loop.dump();
  // fmt::println("{} factor full? {}", unroll_factor, is_full_unroll);
  unroll_it(cfg, loop, unroll_factor, ctx, func, is_full_unroll);
  // fmt::println("{}", func);
  // TODO("okak");
  // fmt::println("===UNROLLED\n{}", func);
  return true;
}

PreservedAnalysis LoopUnroll::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedNC("LoopUnroll", COLOR_OPTIMF);
  CFG &cfg = AnalysisManager::cfg(func);
  Dominators &dom = AnalysisManager::dom(func);
  LoopInfoAnalysis linfo{dom};
  TripCountAnalysis tc{};

  // unrolling one loop invalidates the loop info, so start over but never
  // touch a loop twice
  TVec<fir::BasicBlock> done;
  bool changed = false;
  bool restart = true;
  while (restart) {
    restart = false;
    for (auto &loop : linfo.info) {
      if (std::ranges::contains(done, cfg.bbrs[loop.head].bb)) {
        continue;
      }
      // ensure loop is do while loop
      ScalarEvo evo{cfg, loop};
      if (!tc.update(cfg, dom, loop, evo)) {
        if (peel_condition(cfg, loop, ctx, func, evo)) {
          return PreservedAnalysis::none();
        }
        failure({.reason = "Didnt find loop range",
                 .loc = {cfg.bbrs[loop.head].bb}});
        continue;
      }
      if (!tc.const_exit_iter) {
        failure({.reason = "Trip count is not constant",
                 .loc = {cfg.bbrs[loop.head].bb}});
        continue;
      }
      if (loop.tails.size() != 1 || loop.head != loop.tails[0]) {
        failure({.reason = "Need a simple do while loop",
                 .loc = {cfg.bbrs[loop.head].bb}});
        continue;
      }

      auto head_bb = cfg.bbrs[loop.head].bb;
      if (apply_it(cfg, loop, ctx, func, tc)) {
        utils::StatCollector::get().addi(1, "loopUnrolled",
                                         utils::StatCollector::StatFOptim);
        done.push_back(head_bb);
        changed = true;
        cfg.update(func, false);
        dom.update(cfg);
        linfo.update(dom);
        restart = true;
        break;
      }
    }
  }
  return changed ? PreservedAnalysis::none() : PreservedAnalysis::all();
}

} // namespace foptim::optim
