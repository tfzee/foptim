#pragma once
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {

class Unroll final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function &func) override;
};

inline void unroll_it(CFG &cfg, LoopInfo &loop, u8 unroll_factor,
                      fir::Context &ctx, fir::Function &func) {
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

  ASSERT(unroll_factor <= 8);
  TVec<fir::BasicBlock> new_bbs[8];
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
  }
  // fmt::println("{}", func);
  // TODO("okka");
}

bool apply_it(CFG &cfg, LoopInfo &loop, fir::Context &ctx,
              fir::Function &func) {
  (void)loop;
  (void)ctx;
  (void)func;
  LoopRangeAnalysis range;
  if (!range.update(cfg, loop)) {
    return false;
  }
  if (!range.known_upper || !range.known_lower) {
    return false;
  }
  auto iteration_count = (range.upper_bound - range.lower_bound) / range.a;
  if (iteration_count <= 10 || iteration_count % 2 != 0) {
    return false;
  }
  ASSERT(range.type == LoopRangeAnalysis::IterationType::PlusA);

  u8 unroll_factor = 2;
  if (iteration_count % 8 == 0) {
    unroll_factor = 8;
  }
  if (iteration_count % 4 == 0) {
    unroll_factor = 4;
  }

  // ensure loop is do while loop
  if (loop.tails.size() != 1 || loop.head != loop.tails[0]) {
    return false;
  }

  unroll_it(cfg, loop, unroll_factor, ctx, func);

  // fmt::println("===UNROLLED\n{}", func);
  return true;
}

void Unroll::apply(fir::Context &ctx, fir::Function &func) {
  CFG cfg{func};
  Dominators dom{cfg};
  LoopInfoAnalysis linfo{dom};

  for (auto &loop : linfo.info) {
    if (apply_it(cfg, loop, ctx, func)) {
      // TODO: impl to do multiple loops
      return;
    }
  }
}

} // namespace foptim::optim
