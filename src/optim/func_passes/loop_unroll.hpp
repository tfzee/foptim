#pragma once
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/function_pass.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {

class LoopUnroll final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func) override;
  bool apply_it(CFG &cfg, LoopInfo &loop, fir::Context &ctx,
                fir::Function &func);
};

inline void unroll_it(CFG &cfg, LoopInfo &loop, u8 unroll_factor,
                      fir::Context &ctx, fir::Function &func,
                      bool is_full_unroll) {
  // since the original alraedy exists we substract one of the unroll factor
  unroll_factor -= 1;

  // get which id in the new_bbs as wel as in the body notes matches the head
  // this then we use to redirect the backwards edges to the next part of the
  // loop
  i64 head_id = -1;
  for (u64 i = 0; i < loop.body_nodes.size(); i++) {
    if (loop.body_nodes[i] == loop.head) {
      head_id = (i64)i;
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
      // fmt::println("======================");
      // fmt::println("{}", orig_bb);

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

bool LoopUnroll::apply_it(CFG &cfg, LoopInfo &loop, fir::Context &ctx,
                          fir::Function &func) {
  (void)loop;
  (void)ctx;
  (void)func;
  // fmt::println("==IV2==");
  // loop.dump();
  ScalarEvo evo{cfg, loop};
  LoopBoundsAnalysis lb{};
  // if () {
  //   evo.dump();
  //   lb.dump();
  //   // TODO("nice");
  //   fmt::println("====");
  // }
  // LoopRangeAnalysis range;
  if (!lb.update(evo, cfg, loop)) {
    failure(
        {.reason = "Didnt find loop range", .loc = {cfg.bbrs[loop.head].bb}});
    return false;
  }

  // lb.dump();
  auto iteration_count = lb.n_iter;
  u8 unroll_factor = 2;
  bool is_full_unroll = false;
  if (iteration_count % 8 == 0) {
    unroll_factor = 8;
  } else if (iteration_count % 4 == 0) {
    unroll_factor = 4;
  } else if (iteration_count > 1 && iteration_count <= 6) {
    is_full_unroll = true;
    unroll_factor = iteration_count;
  } else {
    return false;
  }

  size_t n_instrs = 0;
  for (auto i : loop.body_nodes) {
    n_instrs += cfg.bbrs[i].bb->n_instrs();
  }

  if (n_instrs < 4 && iteration_count < 32) {
    return true;
  }
  if (unroll_factor * n_instrs > 50) {
    failure({.reason = "Unroll too massive", .loc = {cfg.bbrs[loop.head].bb}});
    return false;
  }

  // ensure loop is do while loop
  if (loop.tails.size() != 1 || loop.head != loop.tails[0]) {
    failure(
        {.reason = "Need a do while loop", .loc = {cfg.bbrs[loop.head].bb}});
    return false;
  }

  // fmt::println("{}", func);
  // fmt::println("unrollnig!!");
  // loop.dump();
  // fmt::println("{} factor", unroll_factor);
  // fmt::println("{} full?", is_full_unroll);
  unroll_it(cfg, loop, unroll_factor, ctx, func, is_full_unroll);
  // fmt::println("{}", func);
  // TODO("okak");
  // fmt::println("===UNROLLED\n{}", func);
  return true;
}

void LoopUnroll::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedN("unroll");
  CFG cfg{func};
  Dominators dom{cfg};
  LoopInfoAnalysis linfo{dom};

  for (auto &loop : linfo.info) {
    if (apply_it(cfg, loop, ctx, func)) {
      utils::StatCollector::get().addi(1, "loopUnrolled",
                                       utils::StatCollector::StatFOptim);
      // TODO: impl to do multiple loops
      return;
    }
  }
}

} // namespace foptim::optim
