#include "flag_annotator.hpp"

#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"

namespace foptim::optim {

namespace {
using ICmpSub = fir::ICmpInstrSubType;

// x < l with the compare signed or unsigned
struct Less {
  fir::ValueR x;
  fir::ValueR l;
  bool is_signed = false;
};

ICmpSub invert(ICmpSub p) {
  switch (p) {
    case ICmpSub::SLT:
      return ICmpSub::SGE;
    case ICmpSub::SGE:
      return ICmpSub::SLT;
    case ICmpSub::SGT:
      return ICmpSub::SLE;
    case ICmpSub::SLE:
      return ICmpSub::SGT;
    case ICmpSub::ULT:
      return ICmpSub::UGE;
    case ICmpSub::UGE:
      return ICmpSub::ULT;
    case ICmpSub::UGT:
      return ICmpSub::ULE;
    case ICmpSub::ULE:
      return ICmpSub::UGT;
    default:
      return ICmpSub::INVALID;
  }
}

// the strict less than that holds when cmp has the given outcome
bool as_less(fir::Instr cmp, bool outcome, Less &res) {
  if (!cmp->is(fir::InstrType::ICmp)) {
    return false;
  }
  auto p = static_cast<ICmpSub>(cmp->subtype);
  if (!outcome) {
    p = invert(p);
  }
  switch (p) {
    case ICmpSub::SLT:
    case ICmpSub::ULT:
      res = {
          .x = cmp->args[0], .l = cmp->args[1], .is_signed = p == ICmpSub::SLT};
      return true;
    case ICmpSub::SGT:
    case ICmpSub::UGT:
      res = {
          .x = cmp->args[1], .l = cmp->args[0], .is_signed = p == ICmpSub::SGT};
      return true;
    default:
      return false;
  }
}

class Annotator {
  CFG &cfg;
  Dominators &dom;

  [[nodiscard]] bool defined_in(const LoopInfo &loop, fir::ValueR v) const {
    if (v.is_instr()) {
      return loop.contains(cfg.get_bb_id(v.as_instr()->get_parent()));
    }
    if (v.is_bb_arg()) {
      return loop.contains(cfg.get_bb_id(v.as_bb_arg()->get_parent()));
    }
    return false;
  }

  // does x < l hold (same signedness) whenever control passes from block e
  // into da header. looks for a branch edge into a dominating block
  [[nodiscard]] bool holds_on_edge(u32 from, u32 to, const Less &want) const {
    auto term = cfg.bbrs[from].bb->get_terminator();
    if (!term->is(fir::InstrType::CondBranchInstr) ||
        term->bbs[0].bb == term->bbs[1].bb || !term->args[0].is_instr()) {
      return false;
    }
    const bool on_true = term->bbs[0].bb == cfg.bbrs[to].bb;
    Less got;
    return as_less(term->args[0].as_instr(), on_true, got) &&
           got.is_signed == want.is_signed && got.x == want.x &&
           got.l == want.l;
  }

  [[nodiscard]] bool guarded(u32 e, u32 head, const Less &want) const {
    if (holds_on_edge(e, head, want)) {
      return true;
    }
    u32 b = e;
    for (u32 steps = 0; steps < 64; steps++) {
      const auto &preds = cfg.bbrs[b].pred;
      if (preds.size() == 1 && holds_on_edge(preds[0], b, want)) {
        return true;
      }
      const u32 up = dom.idom(b);
      if (up == b || up == ~0U) {
        return false;
      }
      b = up;
    }
    return false;
  }

  // counter of a loop is a header argument, entered with s, passed back as
  // next = arg + 1, the loop continues while next < l and l is invariant.
  // with s < l at the entry every value of the argument is below l, so next
  // does not wrap
  bool counter(const LoopInfo &loop) {
    if (loop.tails.size() != 1) {
      return false;
    }
    auto head = cfg.bbrs[loop.head].bb;
    auto latch = cfg.bbrs[loop.tails[0]].bb;
    auto term = latch->get_terminator();
    if (!term->is(fir::InstrType::CondBranchInstr) ||
        term->bbs[0].bb == term->bbs[1].bb || !term->args[0].is_instr()) {
      return false;
    }
    const bool back_is_true = term->bbs[0].bb == head;
    if (!back_is_true && term->bbs[1].bb != head) {
      return false;
    }
    Less cont;
    if (!as_less(term->args[0].as_instr(), back_is_true, cont) ||
        !cont.x.is_instr() || defined_in(loop, cont.l)) {
      return false;
    }
    fir::Instr next = cont.x.as_instr();
    if (!next->is(fir::BinaryInstrSubType::IntAdd) ||
        !next->args[1].is_constant_int() ||
        next->args[1].as_constant()->as_int() != 1 ||
        !next->args[0].is_bb_arg() ||
        (cont.is_signed ? next->NSW != 0 : next->NUW != 0)) {
      return false;
    }
    auto arg = next->args[0].as_bb_arg();
    if (arg->get_parent() != head) {
      return false;
    }
    size_t idx = 0;
    while (idx < head->args.size() && !(head->args[idx] == arg)) {
      idx++;
    }
    const auto &back = term->bbs[back_is_true ? 0 : 1];
    if (idx >= head->args.size() || idx >= back.args.size() ||
        !(back.args[idx] == next.result())) {
      return false;
    }
    // one entry from outside
    u32 entry = ~0U;
    fir::ValueR start;
    for (auto pred : cfg.bbrs[loop.head].pred) {
      if (loop.contains(pred)) {
        continue;
      }
      auto pterm = cfg.bbrs[pred].bb->get_terminator();
      for (auto &target : pterm->bbs) {
        if (target.bb != head) {
          continue;
        }
        if (entry != ~0U || idx >= target.args.size()) {
          return false;
        }
        entry = pred;
        start = target.args[idx];
      }
    }
    if (entry == ~0U || start.is_invalid()) {
      return false;
    }
    if (!guarded(entry, loop.head,
                 Less{.x = start, .l = cont.l, .is_signed = cont.is_signed})) {
      return false;
    }
    if (cont.is_signed) {
      next->NSW = true;
    } else {
      next->NUW = true;
    }
    return true;
  }

 public:
  Annotator(CFG &cfg, Dominators &dom) : cfg(cfg), dom(dom) {}

  bool run(const LoopInfoAnalysis &loops) {
    bool changed = false;
    for (const auto &loop : loops.info) {
      changed |= counter(loop);
    }
    return changed;
  }
};
}  // namespace

PreservedAnalysis FlagAnnotator::apply(fir::Context & /*ctx*/,
                                       fir::Function &func) {
  ZoneScopedN("FlagAnnotator");
  CFG &cfg = AnalysisManager::cfg(func);
  Dominators &dom = AnalysisManager::dom(func);
  LoopInfoAnalysis loops{dom};
  Annotator a{cfg, dom};
  return a.run(loops) ? PreservedAnalysis::cfg_only()
                      : PreservedAnalysis::all();
}

}  // namespace foptim::optim
