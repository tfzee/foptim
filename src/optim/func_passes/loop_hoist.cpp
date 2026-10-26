#include "loop_hoist.hpp"

#include <algorithm>

#include "ir/builder.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/func_passes/licm.hpp"

namespace foptim::optim {

namespace {

bool in_nodes(const TVec<u32> &nodes, u32 id) {
  return std::ranges::find(nodes, id) != nodes.end();
}

struct HoistPlan {
  u32 pre;
  TVec<std::pair<u32, u16>> exits;
  u32 exit_target;
};

class Hoister {
  fir::Context &ctx;
  fir::Function &func;
  CFG &cfg;
  Dominators &dom;

 public:
  Hoister(fir::Context &ctx, fir::Function &func, CFG &cfg, Dominators &dom)
      : ctx(ctx), func(func), cfg(cfg), dom(dom) {}

  [[nodiscard]] u32 block_of(fir::ValueR v) const {
    if (v.is_instr()) {
      return cfg.get_bb_id(v.as_instr()->get_parent());
    }
    return cfg.get_bb_id(v.as_bb_arg()->get_parent());
  }

  [[nodiscard]] bool invariant(const LoopInfo &outer, const LoopInfo &inner,
                               fir::ValueR v) const {
    if (!v.is_instr() && !v.is_bb_arg()) {
      return true;
    }
    const u32 id = block_of(v);
    return in_nodes(inner.body_nodes, id) || !in_nodes(outer.body_nodes, id);
  }

  static bool plain_instr(fir::Instr instr) {
    if (instr->Volatile || instr->Atomic ||
        instr->is(fir::InstrType::CallInstr) || instr->pot_modifies_mem() ||
        instr->has_pot_sideeffects()) {
      return false;
    }
    return !instr->pot_reads_mem() || instr->is(fir::InstrType::LoadInstr);
  }

  // no instruction that has an effect in front of the terminator
  static bool effect_free(fir::BasicBlock bb) {
    for (auto instr : bb->instructions) {
      if (instr == bb->get_terminator()) {
        break;
      }
      if (instr->pot_modifies_mem() || instr->has_pot_sideeffects() ||
          instr->is_critical()) {
        return false;
      }
    }
    return true;
  }

  bool plan(const LoopInfo &outer, const LoopInfo &inner, HoistPlan &out) {
    if (inner.head == outer.head ||
        inner.body_nodes.size() >= outer.body_nodes.size()) {
      return false;
    }
    if (!std::ranges::all_of(inner.body_nodes, [&](u32 n) {
          return in_nodes(outer.body_nodes, n);
        })) {
      return false;
    }
    // the outer loop is entered from one block through one edge
    if (cfg.bbrs[outer.head].pred.size() != outer.tails.size() + 1) {
      return false;
    }
    u32 outer_pre = ~0U;
    for (auto p : cfg.bbrs[outer.head].pred) {
      if (!in_nodes(outer.body_nodes, p)) {
        outer_pre = p;
      }
    }
    if (outer_pre == ~0U) {
      return false;
    }
    {
      auto term = cfg.bbrs[outer_pre].bb->get_terminator();
      u32 n = 0;
      for (auto &b : term->bbs) {
        n += static_cast<u32>(b.bb == cfg.bbrs[outer.head].bb);
      }
      if (n != 1) {
        return false;
      }
    }
    // inner loop has one entry edge from a block that always runs into it
    u32 pre = ~0U;
    for (u32 n : inner.body_nodes) {
      for (auto p : cfg.bbrs[n].pred) {
        if (in_nodes(inner.body_nodes, p)) {
          continue;
        }
        if (n != inner.head || pre != ~0U) {
          return false;
        }
        pre = p;
      }
    }
    if (pre == ~0U) {
      return false;
    }
    auto pre_term = cfg.bbrs[pre].bb->get_terminator();
    if (!pre_term->is(fir::InstrType::BranchInstr) ||
        pre_term->bbs.size() != 1) {
      return false;
    }
    // all edges that leave the inner loop go to one block of the outer loop
    out.exits.clear();
    for (u32 n : inner.body_nodes) {
      auto term = cfg.bbrs[n].bb->get_terminator();
      for (u32 i = 0; i < term->bbs.size(); i++) {
        const u32 t = cfg.get_bb_id(term->bbs[i].bb);
        if (in_nodes(inner.body_nodes, t)) {
          continue;
        }
        if (!out.exits.empty() && t != out.exit_target) {
          return false;
        }
        out.exits.emplace_back(n, i);
        out.exit_target = t;
      }
    }
    if (out.exits.empty() || !in_nodes(outer.body_nodes, out.exit_target)) {
      return false;
    }
    out.pre = pre;

    // every iteration of the outer loop runs the inner loop
    for (u32 t : outer.tails) {
      if (!dom.dominates(pre, t)) {
        return false;
      }
    }
    for (u32 l : outer.leaving_nodes) {
      if (!dom.dominates(pre, l)) {
        return false;
      }
    }
    // nothing with an effect runs before it in the iteration
    for (u32 n : outer.body_nodes) {
      if ((n == pre || dom.strict_dominates(n, pre)) &&
          !effect_free(cfg.bbrs[n].bb)) {
        return false;
      }
    }

    for (auto arg : pre_term->bbs[0].args) {
      if (!invariant(outer, inner, arg)) {
        return false;
      }
    }
    for (u32 n : inner.body_nodes) {
      auto bb = cfg.bbrs[n].bb;
      for (auto instr : bb->instructions) {
        if (instr != bb->get_terminator() && !plain_instr(instr)) {
          return false;
        }
        for (auto arg : instr->args) {
          if (!invariant(outer, inner, arg)) {
            return false;
          }
        }
        for (auto &b : instr->bbs) {
          for (auto arg : b.args) {
            if (!invariant(outer, inner, arg)) {
              return false;
            }
          }
        }
      }
    }
    for (u32 n : inner.body_nodes) {
      for (auto instr : cfg.bbrs[n].bb->instructions) {
        if (instr->is(fir::InstrType::LoadInstr) &&
            !LICM::sees_memory_of_entry(func, outer, instr)) {
          return false;
        }
      }
    }
    return true;
  }

  void apply(const LoopInfo &outer, const LoopInfo &inner, const HoistPlan &p) {
    auto outer_head = cfg.bbrs[outer.head].bb;
    auto outer_pre = [&] {
      for (auto pr : cfg.bbrs[outer.head].pred) {
        if (!in_nodes(outer.body_nodes, pr)) {
          return cfg.bbrs[pr].bb;
        }
      }
      UNREACH();
    }();
    auto pre_bb = cfg.bbrs[p.pre].bb;
    auto pre_term = pre_bb->get_terminator();
    auto exit_target = cfg.bbrs[p.exit_target].bb;
    auto entry_bb = cfg.bbrs[inner.head].bb;

    TVec<fir::BasicBlock> old_blocks;
    for (u32 n : inner.body_nodes) {
      old_blocks.push_back(cfg.bbrs[n].bb);
    }
    TVec<fir::BasicBlock> others;
    for (auto bb : func.basic_blocks) {
      if (!std::ranges::contains(old_blocks, bb)) {
        others.push_back(bb);
      }
    }

    TVec<fir::ValueR> entry_args{pre_term->bbs[0].args.begin(),
                                 pre_term->bbs[0].args.end()};
    auto outer_pre_term = outer_pre->get_terminator();
    const u16 into_outer = outer_pre_term.get_bb_id(outer_head);
    TVec<fir::ValueR> outer_args{outer_pre_term->bbs[into_outer].args.begin(),
                                 outer_pre_term->bbs[into_outer].args.end()};

    fir::ContextData::V2VMap subs;
    TVec<fir::BasicBlock> copies;
    for (auto bb : old_blocks) {
      copies.push_back(ctx->copy(bb, subs, false, fir::MetaCopy::All));
    }
    for (auto bb : copies) {
      for (auto instr : bb->instructions) {
        instr.substitute(subs);
      }
    }
    const auto copy_of = [&](fir::BasicBlock bb) {
      return subs.at(fir::ValueR(bb)).as_bb();
    };
    auto copy_entry = copy_of(entry_bb);
    // the exits of the copy meet in one block that continues into the outer
    // loop where its preheader went, its arguments are the values the original
    // exit target gets
    fir::Builder eb{outer_pre};
    auto join = eb.append_bb();
    TVec<fir::ValueR> join_args;
    for (auto arg : exit_target->args) {
      join_args.emplace_back(
          join.add_arg(ctx->storage.insert_bb_arg(join, arg->get_type())));
    }
    eb.at_end(join);
    {
      auto br = eb.build_branch(outer_head);
      for (auto a : outer_args) {
        br.add_bb_arg(0, a);
      }
    }
    for (auto [blk, edge] : p.exits) {
      auto t = copy_of(cfg.bbrs[blk].bb)->get_terminator();
      TVec<fir::ValueR> args{t->bbs[edge].args.begin(),
                             t->bbs[edge].args.end()};
      t.replace_bb(edge, join, false);
      for (auto a : args) {
        t.add_bb_arg(edge, a);
      }
    }
    for (auto bb : copies) {
      func.append_bbr(bb);
    }
    // outer preheader runs the copy first
    outer_pre_term.replace_bb(into_outer, copy_entry, false);
    for (auto a : entry_args) {
      outer_pre_term.add_bb_arg(into_outer, a);
    }

    // the original skips the loop, its exit values are the ones of the copy
    pre_term.replace_bb(0, exit_target, false);
    for (auto a : join_args) {
      pre_term.add_bb_arg(0, a);
    }
    for (auto bb : others) {
      for (auto instr : bb->instructions) {
        instr.substitute(subs);
      }
    }

    for (auto bb : old_blocks) {
      for (auto arg : bb->args) {
        if (arg->get_n_uses() > 0) {
          arg->replace_all_uses(
              fir::ValueR{ctx->get_poisson_value(arg->get_type())});
        }
      }
      for (auto instr : bb->instructions) {
        for (u32 r = 0; r < instr->n_results(); r++) {
          auto res = instr.result(r);
          if (res.get_n_uses() > 0) {
            res.replace_all_uses(
                fir::ValueR{ctx->get_poisson_value(instr.result_type(r))});
          }
        }
      }
    }
    for (auto bb : old_blocks) {
      bb->remove_from_parent(true, true, true);
    }
  }
};

}  // namespace

PreservedAnalysis LoopHoist::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedN("LoopHoist");
  CFG &cfg = AnalysisManager::cfg(func);
  Dominators &dom = AnalysisManager::dom(func);
  LoopInfoAnalysis linfo{dom};

  bool changed = false;
  bool restart = true;
  while (restart) {
    restart = false;
    // biggest inner loops first, so a whole nest moves in one piece
    TVec<u32> order;
    for (u32 i = 0; i < linfo.info.size(); i++) {
      order.push_back(i);
    }
    std::ranges::sort(order, [&](u32 a, u32 b) {
      return linfo.info[a].body_nodes.size() > linfo.info[b].body_nodes.size();
    });
    Hoister h{ctx, func, cfg, dom};
    for (u32 oi : order) {
      for (u32 ii : order) {
        HoistPlan p{};
        if (oi == ii || !h.plan(linfo.info[oi], linfo.info[ii], p)) {
          continue;
        }
        h.apply(linfo.info[oi], linfo.info[ii], p);
        AnalysisManager::drop_memory(func);
        changed = true;
        restart = true;
        break;
      }
      if (restart) {
        break;
      }
    }
    if (restart) {
      cfg.update(func, false);
      dom.update(cfg);
      linfo.update(dom);
    }
  }
  return changed ? PreservedAnalysis::none() : PreservedAnalysis::all();
}

}  // namespace foptim::optim
