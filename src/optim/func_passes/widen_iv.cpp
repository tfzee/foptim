#include "widen_iv.hpp"

#include <algorithm>
#include <optional>

#include "ir/basic_block.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/helper/scev_expander.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

namespace {
using fir::BinaryInstrSubType;
using fir::ICmpInstrSubType;
using fir::InstrType;
using fir::ValueR;

constexpr u32 kWide = 64;

bool signed_or_eq(fir::Instr i) {
  if (!i->is(InstrType::ICmp)) {
    return false;
  }
  using S = ICmpInstrSubType;
  const auto s = static_cast<S>(i->subtype);
  return s == S::SLT || s == S::SLE || s == S::SGT || s == S::SGE ||
         s == S::EQ || s == S::NE;
}

// wide counters of one loop, one per step, the others differ by a constant
struct Group {
  SCEVRef start;
  SCEVRef step;
  ValueR arg{};
};

class LoopWidener {
  fir::Context &ctx;
  CFG &cfg;
  LoopInfo &loop;
  ScalarEvo evo;
  TVec<Group> groups;

  bool in_loop(ValueR v) {
    if (v.is_instr()) {
      return loop.contains(cfg.get_bb_id(v.as_instr()->get_parent()));
    }
    if (v.is_bb_arg()) {
      return loop.contains(cfg.get_bb_id(v.as_bb_arg()->get_parent()));
    }
    return false;
  }

  // the wide recurrence of v if there is one: index of its group and the
  // constant that is added to the group counter
  struct Match {
    size_t group;
    i128 diff;
  };

  SCEVRef wide_rec(ValueR v, bool through_ext) {
    if (!v.get_type()->is_int()) {
      return InvalidSCEV;
    }
    const u32 bw = v.get_type()->get_bitwidth();
    SCEVRef e = evo.get(v);
    if (e == InvalidSCEV) {
      return InvalidSCEV;
    }
    if (through_ext) {
      if (bw >= kWide) {
        return InvalidSCEV;
      }
      e = evo.mk_ext(e, true, kWide);
    } else if (bw != kWide) {
      return InvalidSCEV;
    }
    if (!evo.is_addrec(e) || evo.expr(e).bw != kWide ||
        !evo.is_expandable(evo.expr(e).a) ||
        !evo.is_expandable(evo.expr(e).b)) {
      return InvalidSCEV;
    }
    return e;
  }

  std::optional<Match> find_group(SCEVRef rec) {
    // mk_add grows the expression storage, so the fields are copied
    const SCEVRef ra = evo.expr(rec).a;
    const SCEVRef rb = evo.expr(rec).b;
    for (size_t i = 0; i < groups.size(); i++) {
      if (!evo.same_expr(groups[i].step, rb)) {
        continue;
      }
      const SCEVRef d =
          evo.mk_add(ra, evo.mk_mul(groups[i].start, evo.mk_const(-1, kWide)));
      if (evo.is_const(d)) {
        return Match{.group = i, .diff = evo.expr(d).cval_i()};
      }
    }
    return std::nullopt;
  }

  // a compare that reads a narrow counter of a group
  struct CmpPlan {
    fir::Instr cmp;
    u16 pos;
    Match m;
  };

  static bool is_in(fir::Instr i, const TVec<fir::Instr> &l) {
    return std::ranges::find(l, i) != l.end();
  }

  // The values that are the narrow header argument k plus a constant. Empty
  // unless every use of them is a replaced extension or compare another such
  // add or the back edge of k so the argument dies after the rewrite. When
  // it stays alive the wide counter would only be an extra register, and the
  // vectorizer reasons about the narrow counter.
  TVec<ValueR> dying_counter(u16 k, fir::BasicBlock header,
                             fir::Instr tail_term,
                             const TVec<fir::Instr> &sexts,
                             const TVec<fir::Instr> &cmps) {
    TVec<ValueR> closure{ValueR{header->args[k]}};
    for (size_t n = 0; n < closure.size(); n++) {
      const ValueR x = closure[n];
      for (const auto &u : *x.get_uses()) {
        if (u.type == fir::UseType::BBArg) {
          if (u.user != tail_term || u.bbArgId != k) {
            return {};
          }
          continue;
        }
        if (is_in(u.user, sexts) || is_in(u.user, cmps)) {
          continue;
        }
        const auto sub = static_cast<BinaryInstrSubType>(u.user->subtype);
        if (u.user->is(InstrType::BinaryInstr) && u.argId == 0 &&
            (sub == BinaryInstrSubType::IntAdd ||
             sub == BinaryInstrSubType::IntSub) &&
            u.user->args[1].is_constant_int() && u.user->has_one_result()) {
          closure.push_back(u.user.result());
          continue;
        }
        return {};
      }
    }
    for (const auto &e : tail_term->bbs) {
      if (e.bb == header &&
          std::ranges::find(closure, e.args[k]) == closure.end()) {
        return {};
      }
    }
    return closure;
  }

  // the operand of a compare that is a narrow counter with a wide recurrence
  // while the other one is invariant, -1 if there is none
  int cmp_pos(fir::Instr c) {
    for (u16 pos = 0; pos < 2; pos++) {
      const ValueR o = c->args[1 - pos];
      if (o.get_type()->is_int() && !in_loop(o) &&
          o.get_type() == c->args[pos].get_type() &&
          wide_rec(c->args[pos], true) != InvalidSCEV) {
        return pos;
      }
    }
    return -1;
  }

  ValueR constant(i128 v) { return ValueR{ctx->get_constant_int(v, kWide)}; }

  ValueR counter_at(const Match &m, fir::Instr before) {
    if (m.diff == 0) {
      return groups[m.group].arg;
    }
    fir::Builder b(before);
    ValueR v = b.build_binary_op(groups[m.group].arg, constant(m.diff),
                                 BinaryInstrSubType::IntAdd);
    v.as_instr()->NSW = true;
    return v;
  }

 public:
  LoopWidener(fir::Context &ctx, CFG &cfg, Dominators &dom, LoopInfo &loop)
      : ctx(ctx), cfg(cfg), loop(loop), evo(cfg, loop) {
    // the exit test proves that the counter it compares does not wrap while
    // the loop runs, which makes its extension a wide recurrence
    TripCountAnalysis tc;
    if (tc.update(cfg, dom, loop, evo, true) && tc.cmp_rec != InvalidSCEV &&
        (tc.cmp_signed || tc.cmp_unsigned)) {
      evo.assume_no_wrap(tc.cmp_rec, tc.cmp_signed, std::nullopt, tc.cmp_limit);
    }
  }

  bool run() {
    auto header = cfg.bbrs[loop.head].bb;
    auto entry = evo.entry_block();
    if (!entry.is_valid() || loop.tails.size() != 1) {
      return false;
    }
    auto tail = cfg.bbrs[loop.tails[0]].bb;

    TVec<fir::Instr> sexts;
    TVec<fir::Instr> cmps;
    TVec<SCEVRef> sext_recs;
    for (auto b : loop.body_nodes) {
      for (auto i : cfg.bbrs[b].bb->instructions) {
        if (i->is(InstrType::SExt) && i->has_one_result() &&
            i.result_type()->get_bitwidth() == kWide) {
          const SCEVRef r = wide_rec(i.result(), false);
          if (r != InvalidSCEV) {
            sexts.push_back(i);
            sext_recs.push_back(r);
          }
        } else if (signed_or_eq(i) && cmp_pos(i) >= 0) {
          cmps.push_back(i);
        }
      }
    }
    if (sexts.empty()) {
      return false;
    }

    auto entry_term = entry->get_terminator();
    auto tail_term = tail->get_terminator();

    // only counters that die are widened
    TVec<ValueR> widened;
    for (u32 k = 0; k < header->args.size(); k++) {
      const ValueR h{header->args[k]};
      if (h.get_type()->is_int() && h.get_type()->get_bitwidth() < kWide) {
        for (ValueR v : dying_counter(k, header, tail_term, sexts, cmps)) {
          widened.push_back(v);
        }
      }
    }
    {
      TVec<fir::Instr> ks;
      TVec<SCEVRef> kr;
      for (size_t i = 0; i < sexts.size(); i++) {
        if (std::ranges::find(widened, sexts[i]->args[0]) != widened.end()) {
          ks.push_back(sexts[i]);
          kr.push_back(sext_recs[i]);
        }
      }
      sexts = std::move(ks);
      sext_recs = std::move(kr);
    }
    if (sexts.empty()) {
      return false;
    }
    for (SCEVRef r : sext_recs) {
      if (!find_group(r)) {
        groups.push_back({.start = evo.expr(r).a, .step = evo.expr(r).b});
      }
    }

    // the groups are matched before the IR changes, the expressions of the
    // analysis do not follow the rewrite
    TVec<Match> sext_matches;
    for (SCEVRef r : sext_recs) {
      const auto m = find_group(r);
      ASSERT_M(m.has_value(), "the group of a counter was just created");
      sext_matches.push_back(*m);
    }

    TVec<CmpPlan> plans;
    for (auto c : cmps) {
      const auto pos = static_cast<u16>(cmp_pos(c));
      if (std::ranges::find(widened, c->args[pos]) == widened.end()) {
        continue;
      }
      if (auto m = find_group(wide_rec(c->args[pos], true))) {
        plans.push_back({.cmp = c, .pos = pos, .m = *m});
      }
    }

    SCEVExpander ex(ctx, evo, entry_term);
    for (auto &g : groups) {
      g.arg =
          ValueR{ctx->storage.insert_bb_arg(header, ctx->get_int_type(kWide))};
      header->args.push_back(g.arg.as_bb_arg());
      const ValueR start = ex.expand(g.start);
      const ValueR step = ex.expand(g.step);
      for (u32 e = 0; e < entry_term->bbs.size(); e++) {
        if (entry_term->bbs[e].bb == header) {
          entry_term.add_bb_arg(e, start);
        }
      }
      fir::Builder tb(tail_term);
      ValueR next = tb.build_binary_op(g.arg, step, BinaryInstrSubType::IntAdd);
      next.as_instr()->NSW = true;
      for (u32 e = 0; e < tail_term->bbs.size(); e++) {
        if (tail_term->bbs[e].bb == header) {
          tail_term.add_bb_arg(e, next);
        }
      }
    }

    for (size_t k = 0; k < sexts.size(); k++) {
      sexts[k].result().replace_all_uses(counter_at(sext_matches[k], sexts[k]));
      sexts[k].destroy();
    }
    for (auto &p : plans) {
      const ValueR o = p.cmp->args[1 - p.pos];
      ValueR wide_o;
      if (o.is_constant_int()) {
        wide_o = constant(o.as_constant()->as_int());
      } else {
        fir::Builder eb(entry_term);
        wide_o = eb.build_sext(o, ctx->get_int_type(kWide));
      }
      ValueR cnt = counter_at(p.m, p.cmp);
      fir::Builder cb(p.cmp);
      const auto pred = static_cast<ICmpInstrSubType>(p.cmp->subtype);
      ValueR c = p.pos == 0 ? cb.build_int_cmp(cnt, wide_o, pred)
                            : cb.build_int_cmp(wide_o, cnt, pred);
      p.cmp.result().replace_all_uses(c);
      p.cmp.destroy();
    }
    return true;
  }
};
}  // namespace

PreservedAnalysis WidenIV::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedN("WidenIV");
  CFG &cfg = AnalysisManager::cfg(func);
  Dominators &dom = AnalysisManager::dom(func);
  LoopInfoAnalysis linfo{dom};
  bool changed = false;
  for (auto &loop : linfo.info) {
    LoopWidener w{ctx, cfg, dom, loop};
    changed |= w.run();
  }
  return changed ? PreservedAnalysis::cfg_only() : PreservedAnalysis::all();
}

}  // namespace foptim::optim
