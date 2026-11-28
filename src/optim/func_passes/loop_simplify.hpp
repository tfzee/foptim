#pragma once
#include <fmt/base.h>
#include <fmt/core.h>

#include <algorithm>
#include <optional>

#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/analysis/mem/alias_query.hpp"
#include "optim/analysis/mem/mem_access.hpp"
#include "optim/function_pass.hpp"
#include "optim/helper/scev_expander.hpp"
#include "utils/set.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {
class LoopSimplify final : public FunctionPass {
 public:
  // calls f(value) for every value defined in the loop that is used outside of
  // it (including as an argument of an edge leaving the loop)
  template <class F>
  static void for_each_outside_used(CFG &cfg, const LoopInfo &loop, F &&f) {
    auto outside = [&](const fir::Use &u) {
      if (u.type == fir::UseType::BBArg) {
        return !loop.contains(cfg.get_bb_id(u.user->bbs[u.argId].bb));
      }
      return !loop.contains(cfg.get_bb_id(u.user->get_parent()));
    };
    for (auto b : loop.body_nodes) {
      for (auto arg : cfg.bbrs[b].bb->args) {
        if (std::ranges::any_of(arg->uses, outside)) {
          f(fir::ValueR{arg});
        }
      }
      for (auto i : cfg.bbrs[b].bb->instructions) {
        if (std::ranges::any_of(i->uses, outside)) {
          for (u8 r = 0; r < std::max<u8>(i->n_results(), 1); r++) {
            f(i.is_multi_result() ? i.result(r) : i.result());
          }
        }
      }
    }
  }

  // identity of everything const_exit_value looks at: the loop body, the
  // users of its values after the loop and the entry edges
  static u64 loop_fingerprint(CFG &cfg, const LoopInfo &loop) {
    using ankerl::unordered_dense::detail::wyhash::mix;
    constexpr u64 K = UINT64_C(0x9E3779B97F4A7C15);
    u64 h = loop.body_nodes.size();
    auto outside = [&](const fir::Use &u) {
      if (u.type == fir::UseType::BBArg) {
        return !loop.contains(cfg.get_bb_id(u.user->bbs[u.argId].bb));
      }
      return !loop.contains(cfg.get_bb_id(u.user->get_parent()));
    };
    auto fold_uses = [&](const auto &val) {
      for (const auto &u : val->uses) {
        if (outside(u)) {
          h = mix(h ^ AnalysisManager::hash_instr(u.user), K);
        }
      }
    };
    for (auto b : loop.body_nodes) {
      auto bb = cfg.bbrs[b].bb;
      h = mix(h ^ std::hash<fir::BasicBlock>()(bb), K);
      for (auto arg : bb->args) {
        h = mix(h ^ std::hash<fir::BBArgument>()(arg), K);
        fold_uses(arg);
      }
      for (auto i : bb->instructions) {
        h = mix(h ^ AnalysisManager::hash_instr(i), K);
        fold_uses(i);
      }
    }
    for (auto p : cfg.bbrs[loop.head].pred) {
      if (!loop.contains(p)) {
        h = mix(h ^ AnalysisManager::hash_instr(cfg.bbrs[p].bb->get_terminator()),
                K);
      }
    }
    return h;
  }

  static bool has_sideeffects(CFG &cfg, const LoopInfo &loop) {
    for (auto b : loop.body_nodes) {
      for (auto i : cfg.bbrs[b].bb->instructions) {
        if (i->has_pot_sideeffects()) {
          return true;
        }
      }
    }
    return false;
  }

  bool dead_loop_elimination(CFG &cfg, LoopInfoAnalysis &loops, u32 loop_i) {
    auto &loop = loops.info.at(loop_i);
    if (has_sideeffects(cfg, loop)) {
      return false;
    }
    bool used_outside = false;
    for_each_outside_used(cfg, loop, [&](fir::ValueR) { used_outside = true; });
    if (used_outside) {
      return false;
    }
    if (loop.leaving_nodes.size() == 1) {
      auto leaver_id = loop.leaving_nodes[0];
      auto leaver_term = cfg.bbrs[leaver_id].bb->get_terminator();
      if (!leaver_term->is(fir::InstrType::CondBranchInstr)) {
        return false;
      }
      fir::Builder bb{leaver_term};
      auto leave_index = 0;
      if (loop.contains(cfg.get_bb_id(leaver_term->bbs[0].bb))) {
        leave_index = 1;
      }
      auto new_branch = bb.build_branch(leaver_term->bbs[leave_index].bb);
      for (auto a : leaver_term->bbs[leave_index].args) {
        new_branch.add_bb_arg(0, a);
      }
      leaver_term.destroy();
      utils::StatCollector::get().addi(1, "DeadLoopElim",
                                       utils::StatCollector::StatFOptim);
      return true;
    }
    return false;
  }

  bool propagate_to_after_loop(CFG &cfg, Dominators &dom,
                               LoopInfoAnalysis &loops, u32 loop_i) {
    // propagate side effect free expressions that depend on other expresions
    auto &loop = loops.info[loop_i];
    TSet<fir::BasicBlock> body_nodes_lut;
    for (auto b_id : loop.body_nodes) {
      body_nodes_lut.insert(cfg.bbrs[b_id].bb);
    }
    bool modified = false;
    // expressions of loaded values stay in the loop, copying them behind it
    // keeps the loads alive there and hides reductions from the vectorizer
    TMap<fir::Instr, bool> loaded;
    auto depends_on_load = [&](auto &self, fir::Instr i) -> bool {
      if (auto it = loaded.find(i); it != loaded.end()) {
        return it->second;
      }
      loaded.insert({i, false});
      bool res = i->pot_reads_mem();
      for (auto a : i->args) {
        if (!res && a.is_instr() &&
            body_nodes_lut.contains(a.as_instr()->get_parent())) {
          res = self(self, a.as_instr());
        }
      }
      loaded[i] = res;
      return res;
    };
    for (auto b_id : loop.body_nodes) {
      for (auto instr : cfg.bbrs[b_id].bb->instructions) {
        // TODO(PERF): could move loads but only if theres no interfering writes
        // before this instruction and any potential exit
        if (instr->has_pot_sideeffects() || instr->pot_reads_mem() ||
            instr->pot_modifies_mem() || instr->is_critical() ||
            depends_on_load(depends_on_load, instr)) {
          continue;
        }
        TVec<fir::Use> uses;
        for (auto use : instr->uses) {
          if (body_nodes_lut.contains(use.user->get_parent())) {
            continue;
          }
          uses.push_back(use);
        }
        // a copy serves every use it dominates, so chains of copies do not
        // expand shared subexpressions into trees
        std::sort(uses.begin(), uses.end(),
                  [&](const fir::Use &a, const fir::Use &b) {
                    auto ba = a.user->get_parent();
                    auto bb = b.user->get_parent();
                    if (ba != bb) {
                      return cfg.get_bb_id(ba) < cfg.get_bb_id(bb);
                    }
                    return ba->get_instr_id(a.user) < ba->get_instr_id(b.user);
                  });
        TVec<fir::Instr> copies;
        for (auto use : uses) {
          auto use_bb = use.user->get_parent();
          fir::Instr shared = fir::Instr{};
          bool found = false;
          for (auto c : copies) {
            auto c_bb = c->get_parent();
            if (c_bb == use_bb
                    ? c_bb->get_instr_id(c) < c_bb->get_instr_id(use.user)
                    : dom.strict_dominates(c_bb, use_bb)) {
              shared = c;
              found = true;
              break;
            }
          }
          if (!found) {
            fir::Builder b{use.user};
            shared = b.insert_copy(instr, fir::MetaCopy::Types);
            copies.push_back(shared);
          }
          use.replace_use(shared.result());
          modified = true;
          utils::StatCollector::get().addi(1, "PropagateUseAfter",
                                           utils::StatCollector::StatFOptim);
        }
      }
    }
    return modified;
  }

  bool merge_same_diret_induct_var(CFG &cfg, LoopInfoAnalysis &loops,
                                   u32 loop_i) {
    // two header arguments {s1,+,c1} (kept) and {s2,+,c2} with c2 = r * c1 and
    // s2 = r * s1 are the same induction variable, the second one can be
    // computed from the first (truncated to the narrower type if needed)
    auto &loop = loops.info[loop_i];
    if (cfg.bbrs[loop.head].pred.size() != loop.tails.size() + 1) {
      return false;
    }

    auto *ctx = cfg.func->ctx;
    ScalarEvo evo{cfg, loop};
    auto head_bb = cfg.bbrs[loop.head].bb;
    auto args = head_bb->args;
    for (size_t i1 = 0; i1 < args.size(); i1++) {
      for (size_t i2 = i1 + 1; i2 < args.size(); i2++) {
        const fir::ValueR v1{args[i1]};
        const fir::ValueR v2{args[i2]};
        if (!v1.get_type()->is_int() || !v2.get_type()->is_int()) {
          continue;
        }
        const SCEVRef r1 = evo.get(v1);
        const SCEVRef r2 = evo.get(v2);
        if (!evo.is_const_addrec(r1) || !evo.is_const_addrec(r2)) {
          continue;
        }

        // keep the wider one
        auto keep = v2;
        auto repl = v1;
        auto rkeep = r2;
        auto rrepl = r1;
        if (v1.get_type()->get_bitwidth() > v2.get_type()->get_bitwidth()) {
          keep = v1;
          repl = v2;
          rkeep = r1;
          rrepl = r2;
        }
        const i128 step_keep = evo.expr(evo.expr(rkeep).b).cval_i();
        const i128 step_repl = evo.expr(evo.expr(rrepl).b).cval_i();
        const i128 start_keep = evo.expr(evo.expr(rkeep).a).cval_i();
        const i128 start_repl = evo.expr(evo.expr(rrepl).a).cval_i();
        const u32 repl_bw = repl.get_type()->get_bitwidth();
        // the constants are compared as numbers of at most 64 bits
        if (repl_bw > 64 || keep.get_type()->get_bitwidth() > 64) {
          continue;
        }
        if (step_keep == 0 || step_repl % step_keep != 0) {
          continue;
        }
        const i128 ratio = step_repl / step_keep;
        // trunc(keep) * ratio has to start where repl starts
        const i128 expected_start = static_cast<i128>(
            static_cast<u128>(start_keep) * static_cast<u128>(ratio));
        if (!APInt::from_i128(repl_bw, expected_start - start_repl).is_zero()) {
          continue;
        }

        fir::Builder b{head_bb};
        fir::ValueR new_repl = keep;
        if (keep.get_type()->get_bitwidth() != repl_bw) {
          new_repl = b.build_itrunc(keep, repl.get_type());
        }
        if (ratio != 1) {
          new_repl = b.build_int_mul(
              new_repl,
              fir::ValueR{ctx->get_constant_value(ratio, repl.get_type())});
        }
        repl.replace_all_uses(new_repl);

        return true;
      }
    }

    return false;
  }

  // Proof by induction that a header argument is the same value as a pure
  // expression X of the header arguments that is computed in the loop: X with
  // every header argument replaced by its start value has to be the start value
  // of the argument, and X with every header argument replaced by its back edge
  // value has to be the back edge value of the argument. The trees are compared
  // structurally (adds of zero and multiplies by zero on the X side are
  // folded). The poison flags of the argument side have to be within the ones
  // of X, so replacing X by the argument cannot add poison.
  struct HeaderEquiv {
    CFG &cfg;
    const LoopInfo &loop;
    fir::BasicBlock header;
    TVec<fir::ValueR> start;
    TVec<fir::ValueR> back;
    static constexpr u32 MaxDepth = 8;

    [[nodiscard]] static bool pure_kind(fir::Instr i) {
      using BT = fir::BinaryInstrSubType;
      if (i->is(fir::InstrType::SExt) || i->is(fir::InstrType::ZExt) ||
          i->is(fir::InstrType::ITrunc)) {
        return i->args.size() == 1;
      }
      if (!i->is(fir::InstrType::BinaryInstr) || i->args.size() != 2) {
        return false;
      }
      switch (static_cast<BT>(i->subtype)) {
        case BT::PtrAdd:
        case BT::IntAdd:
        case BT::IntSub:
        case BT::IntMul:
        case BT::Shl:
        case BT::And:
        case BT::Or:
        case BT::Xor:
          return true;
        default:
          return false;
      }
    }

    [[nodiscard]] bool in_loop(fir::ValueR v) const {
      if (v.is_instr()) {
        return loop.contains(cfg.get_bb_id(v.as_instr()->get_parent()));
      }
      if (v.is_bb_arg()) {
        return loop.contains(cfg.get_bb_id(v.as_bb_arg()->get_parent()));
      }
      return false;
    }

    [[nodiscard]] i64 header_index(fir::ValueR v) const {
      if (!v.is_bb_arg() || v.as_bb_arg()->get_parent() != header) {
        return -1;
      }
      for (size_t k = 0; k < header->args.size(); k++) {
        if (header->args[k] == v.as_bb_arg()) {
          return static_cast<i64>(k);
        }
      }
      return -1;
    }

    [[nodiscard]] static bool flags_within(fir::Instr q, fir::Instr p) {
      return (!q->NSW || p->NSW) && (!q->NUW || p->NUW) &&
             (!q->InBounds || p->InBounds) && (!q->Exact || p->Exact) &&
             (!q->Disjoint || p->Disjoint) && (!q->NNeg || p->NNeg) &&
             (!q->SameSign || p->SameSign);
    }

    // p and q are the same operation on operands rec accepts, q is the side
    // whose poison flags have to be within the ones of p
    template <class F>
    [[nodiscard]] static bool same_op(fir::Instr p, fir::Instr q, F &&rec) {
      if (!pure_kind(p) || p->instr_type != q->instr_type ||
          p->subtype != q->subtype || p.result_type() != q.result_type() ||
          p->args.size() != q->args.size() || !flags_within(q, p)) {
        return false;
      }
      if (p->args.size() == 1) {
        return rec(p->args[0], q->args[0]);
      }
      if (rec(p->args[0], q->args[0]) && rec(p->args[1], q->args[1])) {
        return true;
      }
      return p->is_commutative() && rec(p->args[0], q->args[1]) &&
             rec(p->args[1], q->args[0]);
    }

    [[nodiscard]] bool plain_eq(fir::ValueR p, fir::ValueR q, u32 d) const {
      if (p == q) {
        return true;
      }
      if (d == 0 || !p.is_instr() || !q.is_instr()) {
        return false;
      }
      return same_op(p.as_instr(), q.as_instr(),
                     [&](auto x, auto y) { return plain_eq(x, y, d - 1); });
    }

    [[nodiscard]] bool zero_sub(fir::ValueR v, bool at_start, u32 d) const {
      if (v.is_constant_int(0)) {
        return true;
      }
      if (const i64 j = header_index(v); j >= 0) {
        const auto s = at_start ? start[j] : back[j];
        return s.is_constant_int(0);
      }
      if (d == 0 || !v.is_instr() || !in_loop(v)) {
        return false;
      }
      auto i = v.as_instr();
      return i->is(fir::BinaryInstrSubType::IntMul) &&
             (zero_sub(i->args[0], at_start, d - 1) ||
              zero_sub(i->args[1], at_start, d - 1));
    }

    // a is the tree of X (header arguments are replaced), b is plain
    [[nodiscard]] bool eq_sub(fir::ValueR a, fir::ValueR b, bool at_start,
                              u32 d) const {
      if (const i64 j = header_index(a); j >= 0) {
        return plain_eq(at_start ? start[j] : back[j], b, d);
      }
      if (!in_loop(a)) {
        return plain_eq(a, b, d);
      }
      if (d == 0 || !a.is_instr() || !pure_kind(a.as_instr())) {
        return false;
      }
      auto ai = a.as_instr();
      if (ai->is(fir::BinaryInstrSubType::IntAdd) ||
          ai->is(fir::BinaryInstrSubType::PtrAdd)) {
        if (zero_sub(ai->args[1], at_start, d - 1)) {
          return eq_sub(ai->args[0], b, at_start, d - 1);
        }
        if (ai->is(fir::BinaryInstrSubType::IntAdd) &&
            zero_sub(ai->args[0], at_start, d - 1)) {
          return eq_sub(ai->args[1], b, at_start, d - 1);
        }
      }
      if (ai->is(fir::BinaryInstrSubType::IntMul) &&
          (zero_sub(ai->args[0], at_start, d - 1) ||
           zero_sub(ai->args[1], at_start, d - 1))) {
        return b.is_constant_int(0);
      }
      if (!b.is_instr()) {
        return false;
      }
      return same_op(ai, b.as_instr(), [&](auto x, auto y) {
        return eq_sub(x, y, at_start, d - 1);
      });
    }

    [[nodiscard]] bool equivalent(fir::Instr x, u32 arg) const {
      return eq_sub(x.result(), back[arg], false, MaxDepth) &&
             eq_sub(x.result(), start[arg], true, MaxDepth);
    }
  };

  // Two pointer header arguments that always differ by a constant (j and
  // prev = j - 4 of an insertion sort), the first becomes the second plus the
  // constant. The shapes of the start and back edge values are compared with
  // the first argument written as the second plus the constant.
  static bool merge_offset_header_args(fir::BasicBlock header,
                                       const HeaderEquiv &eq) {
    const size_t n_args = header->args.size();
    for (size_t a = 0; a < n_args; a++) {
      const fir::ValueR va{header->args[a]};
      if (!va.get_type()->is_ptr() || va.get_n_uses() == 0) {
        continue;
      }
      for (size_t b = 0; b < n_args; b++) {
        const fir::ValueR vb{header->args[b]};
        if (a == b || vb.get_type() != va.get_type()) {
          continue;
        }
        const auto sa = decompose_ptr(eq.start[a]);
        const auto sb = decompose_ptr(eq.start[b]);
        if (!sa.same_base(sb)) {
          continue;
        }
        const i64 delta = static_cast<i64>(static_cast<u64>(sa.offset) -
                                           static_cast<u64>(sb.offset));
        // shape of a back edge value, a is b + delta
        auto shape = [&](fir::ValueR v) {
          auto sh = decompose_ptr(v);
          if (sh.base == va) {
            sh.base = vb;
            sh.offset = static_cast<i64>(static_cast<u64>(sh.offset) +
                                         static_cast<u64>(delta));
          }
          return sh;
        };
        const auto ba = shape(eq.back[a]);
        const auto bb = shape(eq.back[b]);
        if (!ba.same_base(bb) ||
            static_cast<u64>(ba.offset) - static_cast<u64>(bb.offset) !=
                static_cast<u64>(delta)) {
          continue;
        }
        auto *ctx = header->get_parent()->ctx;
        fir::Builder bld{header};
        bld.at_start(header);
        fir::ValueR repl = vb;
        if (delta != 0) {
          repl = bld.build_ptr_add(
              vb, fir::ValueR{ctx->get_constant_int(delta, 64)});
        }
        auto to_replace = va;
        to_replace.replace_all_uses(repl);
        utils::StatCollector::get().addi(1, "LoopHeaderArgMerged",
                                         utils::StatCollector::StatFOptim);
        return true;
      }
    }
    return false;
  }

  // Replaces a pure expression of the header arguments by the header argument
  // that is the same value in every iteration, so loads and stores through
  // the two forms of one address (a stepped pointer and base + index * size)
  // get the same pointer value.
  bool merge_redundant_header_args(CFG &cfg, const LoopInfo &loop) {
    constexpr size_t MaxHeaderArgs = 64;
    auto &preds = cfg.bbrs[loop.head].pred;
    if (loop.tails.size() != 1 || preds.size() != 2) {
      return false;
    }
    auto header = cfg.bbrs[loop.head].bb;
    const size_t n_args = header->args.size();
    if (n_args == 0 || n_args > MaxHeaderArgs) {
      return false;
    }
    HeaderEquiv eq{cfg, loop, header, {}, {}};
    eq.start.resize(n_args);
    eq.back.resize(n_args);
    for (const u32 p : preds) {
      auto term = cfg.bbrs[p].bb->get_terminator();
      u32 n_edges = 0;
      for (size_t i = 0; i < term->bbs.size(); i++) {
        if (term->bbs[i].bb != header) {
          continue;
        }
        n_edges++;
        const auto &edge = term->bbs[i].args;
        if (edge.size() != n_args) {
          return false;
        }
        for (size_t k = 0; k < n_args; k++) {
          if (edge[k].is_invalid()) {
            return false;
          }
          (loop.contains(p) ? eq.back : eq.start)[k] = edge[k];
        }
      }
      if (n_edges != 1) {
        return false;
      }
    }

    bool changed = merge_offset_header_args(header, eq);
    for (const u32 b : loop.body_nodes) {
      auto bb = cfg.bbrs[b].bb;
      // instructions are only destroyed by later passes, the uses move
      for (auto x : bb->instructions) {
        if (!HeaderEquiv::pure_kind(x) || x->uses.empty()) {
          continue;
        }
        const auto ty = x.result_type();
        if (!ty->is_int() && !ty->is_ptr()) {
          continue;
        }
        for (size_t k = 0; k < n_args; k++) {
          const fir::ValueR arg{header->args[k]};
          if (arg.get_type() != ty || !eq.back[k].is_instr()) {
            continue;
          }
          auto back_instr = eq.back[k].as_instr();
          if (back_instr == x || back_instr->instr_type != x->instr_type ||
              back_instr->subtype != x->subtype) {
            continue;
          }
          if (eq.equivalent(x, k)) {
            x.result().replace_all_uses(arg);
            utils::StatCollector::get().addi(1, "LoopHeaderArgMerged",
                                             utils::StatCollector::StatFOptim);
            changed = true;
            break;
          }
        }
      }
    }
    return changed;
  }

  bool const_exit_value(fir::Context &ctx, CFG &cfg, Dominators &dom,
                        LoopInfoAnalysis &loops, u32 loop_i,
                        TSet<u64> &no_change) {
    // if we can calculate the exit value of an induction variable (a constant
    // or a closed form of loop invariants) we can "propagate" it into the
    // blocks following the loop
    auto &loop = loops.info[loop_i];
    // a restart of the loop driver asks the unchanged loops again
    const u64 print = loop_fingerprint(cfg, loop);
    if (no_change.contains(print)) {
      return false;
    }
    if (const_exit_value_impl(ctx, cfg, dom, loop)) {
      return true;
    }
    no_change.insert(print);
    return false;
  }

  bool const_exit_value_impl(fir::Context &ctx, CFG &cfg, Dominators &dom,
                             LoopInfo &loop) {
    ScalarEvo evo{cfg, loop};
    InductionEndValueAnalysis iranal{cfg, dom, loop, evo};
    if (iranal.info.empty()) {
      return false;
    }
    bool modified = false;
    for (const auto &v : iranal.info) {
      // Closed forms that are not constants cost instructions in front of the
      // loop, they are only worth it if nothing is left that keeps the loop
      // alive: no side effects and every value used after the loop has one.
      bool removable = !has_sideeffects(cfg, loop);
      for_each_outside_used(cfg, loop, [&](fir::ValueR used) {
        removable = removable &&
                    (v.values.contains(used) || v.ptr_values.contains(used));
      });
      // closed forms are computed in the block that enters the loop
      std::optional<SCEVExpander> expander;
      TMap<fir::ValueR, fir::ValueR> end_values;
      auto end_value = [&](fir::ValueR base) -> std::optional<fir::ValueR> {
        if (auto it = end_values.find(base); it != end_values.end()) {
          return it->second;
        }
        if (auto pit = v.ptr_values.find(base); pit != v.ptr_values.end()) {
          // the pointer stays derived from the pointer in front of the loop
          if (!removable || !evo.entry_block().is_valid()) {
            return std::nullopt;
          }
          if (!expander) {
            expander.emplace(ctx, evo, evo.entry_block()->get_terminator());
          }
          const fir::ValueR off = expander->expand(pit->second.offset);
          fir::Builder pb{evo.entry_block()->get_terminator()};
          const fir::ValueR res = pb.build_ptr_add(pit->second.base, off);
          end_values.insert({base, res});
          return res;
        }
        const SCEVRef r = v.values.at(base);
        fir::ValueR res;
        if (!evo.is_const(r) && !removable) {
          return std::nullopt;
        } else if (evo.is_const(r)) {
          res = fir::ValueR{ctx->get_constant_int(
              evo.expr(r).cval_i(), base.get_type()->get_bitwidth())};
        } else {
          if (!evo.entry_block().is_valid()) {
            return std::nullopt;
          }
          if (!expander) {
            expander.emplace(ctx, evo, evo.entry_block()->get_terminator());
          }
          res = expander->expand(r);
        }
        end_values.insert({base, res});
        return res;
      };

      auto term = v.from_bb->get_terminator();
      auto out_bbarg_id = term.get_bb_id(v.to_bb);
      auto out_bb_id = cfg.get_bb_id(v.to_bb);

      auto &out_bb_arg = term->bbs[out_bbarg_id];
      for (u32 bb_arg_id = 0; bb_arg_id < out_bb_arg.args.size(); bb_arg_id++) {
        if (v.values.contains(out_bb_arg.args[bb_arg_id]) ||
            v.ptr_values.contains(out_bb_arg.args[bb_arg_id])) {
          const auto val = end_value(out_bb_arg.args[bb_arg_id]);
          if (!val) {
            continue;
          }
          term.replace_bb_arg(out_bbarg_id, bb_arg_id, *val, true);
          modified = true;
        }
      }

      TSet<fir::BasicBlock> dominated_blocks;
      for (u32 b_id = 0; b_id < cfg.bbrs.size(); b_id++) {
        if (!dom.dominates(out_bb_id, b_id)) {
          continue;
        }
        dominated_blocks.insert(cfg.bbrs[b_id].bb);
      }

      TVec<fir::ValueR> ended;
      for (const auto &[base, known_val] : v.values) {
        ended.push_back(base);
      }
      for (const auto &[base, known_val] : v.ptr_values) {
        ended.push_back(base);
      }
      for (const auto &base : ended) {
        auto n_uses = base.get_n_uses();
        for (auto uidp1 = n_uses; uidp1 > 0; uidp1--) {
          auto use = (*base.get_uses())[uidp1 - 1];
          // the exit block can dominate the loop itself (nested loops), the
          // uses inside the loop see the per iteration values
          if (loop.contains(cfg.get_bb_id(use.user->get_parent()))) {
            continue;
          }
          if (dominated_blocks.contains(use.user->get_parent())) {
            const auto val = end_value(base);
            if (!val) {
              continue;
            }
            use.replace_use(*val);
            modified = true;
          }
        }
      }
    }

    if (modified) {
      utils::StatCollector::get().addi(1, "LoopExitValProp",
                                       utils::StatCollector::StatFOptim);
    }
    return modified;
  }

  // may instr in any iteration write (or with reads also read) what ref
  // accesses, alias scopes are not used since they only hold in one iteration.
  // span is the loop, so accesses the loop does not move (the jammed lanes of
  // one accumulator) are still compared by their offsets
  static bool touches(AliasQuery &aq, fir::Instr instr, const MemAccess &a,
                      fir::Instr ref, const MemLoc &loc, bool reads,
                      const IterSpan &span) {
    if (a.is_load_store() && !a.ordered) {
      if (a.kind == MemAccess::Kind::Load && !reads) {
        return false;
      }
      return aq.alias(instr, ref, true, &span) != AliasResult::No;
    }
    auto m = aq.mod_ref(instr, loc);
    return reads ? m != ModRef::NoModRef : may_write(m);
  }

  // moves a store to an invariant address that nothing else in the loop touches
  // to a new block on every exit edge, the store has to run before every exit
  bool sink_store_to_exits(fir::Function &func, CFG &cfg, Dominators &dom,
                           const LoopInfo &loop) {
    auto in_loop = [&](u32 id) { return loop.contains(id); };
    // unreachable loops have no header in their body
    if (!in_loop(loop.head)) {
      return false;
    }
    AliasQuery aq{AnalysisManager::provenance(func)};
    const IterSpan span{&cfg, &loop};
    for (auto sb : loop.body_nodes) {
      for (auto store : cfg.bbrs[sb].bb->instructions) {
        if (!store->is(fir::InstrType::StoreInstr)) {
          continue;
        }
        auto ptr = store->args[0];
        auto parent_id = [&](fir::ValueR v) -> std::optional<u32> {
          if (v.is_instr()) {
            return cfg.get_bb_id(v.as_instr()->get_parent());
          }
          if (v.is_bb_arg()) {
            return cfg.get_bb_id(v.as_bb_arg()->get_parent());
          }
          return std::nullopt;
        };
        if (auto d = parent_id(ptr); d && in_loop(*d)) {
          continue;
        }
        auto access = classify_access(store);
        if (!access || access->ordered || access->loc.size == 0) {
          continue;
        }
        bool ok = true;
        TVec<std::pair<fir::Instr, u16>> exits;
        for (auto b : loop.body_nodes) {
          auto term = cfg.bbrs[b].bb->get_terminator();
          for (u16 i = 0; i < term->bbs.size(); i++) {
            if (!in_loop(cfg.get_bb_id(term->bbs[i].bb))) {
              if (!dom.dominates(sb, b)) {
                ok = false;
              }
              exits.emplace_back(term, i);
            }
          }
        }
        if (!ok || exits.empty()) {
          continue;
        }
        for (auto b : loop.body_nodes) {
          for (auto instr : cfg.bbrs[b].bb->instructions) {
            if (instr == store) {
              continue;
            }
            auto a = classify_access(instr);
            if (a && touches(aq, instr, *a, store, access->loc, true, span)) {
              ok = false;
              break;
            }
          }
          if (!ok) {
            break;
          }
        }
        if (!ok) {
          continue;
        }
        auto value = store->args[1];
        for (auto [term, i] : exits) {
          TVec<fir::ValueR> old_args{term->bbs[i].args.begin(),
                                     term->bbs[i].args.end()};
          auto target = term->bbs[i].bb;
          fir::Builder bld{target};
          auto n = bld.append_bb();
          bld.at_end(n);
          bld.build_store(ptr, value, false, false);
          auto br = bld.build_branch(target);
          for (auto a : old_args) {
            br.add_bb_arg(0, a);
          }
          term.replace_bb(i, n, false);
        }
        store.destroy();
        utils::StatCollector::get().addi(1, "LoopStoreSink",
                                         utils::StatCollector::StatFOptim);
        return true;
      }
    }
    return false;
  }

  // a header load from an invariant address whose only writer in the loop is
  // one store after it (run every iteration) becomes a header argument, the
  // store stays. Behind a guard the entry edge gets its own block for the
  // initial load (cfg_changed).
  bool promote_memory_accumulator(fir::Context &ctx, fir::Function &func,
                                  CFG &cfg, Dominators &dom,
                                  const LoopInfo &loop, bool &cfg_changed) {
    auto in_loop = [&](u32 id) { return loop.contains(id); };
    auto &preds = cfg.bbrs[loop.head].pred;
    if (!in_loop(loop.head) || preds.size() != loop.tails.size() + 1) {
      return false;
    }
    fir::BasicBlock pre{fir::BasicBlock::invalid()};
    for (auto p : preds) {
      if (!in_loop(p)) {
        pre = cfg.bbrs[p].bb;
      }
    }
    if (!pre.is_valid()) {
      return false;
    }
    auto header = cfg.bbrs[loop.head].bb;
    const bool guarded =
        !pre->get_terminator()->is(fir::InstrType::BranchInstr);
    if (guarded) {
      // a guard with the header on one edge only
      auto term = pre->get_terminator();
      if (!term->is(fir::InstrType::CondBranchInstr) ||
          std::ranges::count_if(
              term->bbs, [&](const auto &e) { return e.bb == header; }) != 1) {
        return false;
      }
    }
    AliasQuery aq{AnalysisManager::provenance(func)};
    const IterSpan span{&cfg, &loop};

    for (auto load : header->instructions) {
      if (!load->is(fir::InstrType::LoadInstr)) {
        continue;
      }
      auto ptr = load->args[0];
      if (ptr.is_instr()) {
        if (in_loop(cfg.get_bb_id(ptr.as_instr()->get_parent()))) {
          continue;
        }
      } else if (ptr.is_bb_arg()) {
        if (in_loop(cfg.get_bb_id(ptr.as_bb_arg()->get_parent()))) {
          continue;
        }
      }
      auto access = classify_access(load);
      if (!access || access->ordered || access->loc.size == 0) {
        continue;
      }

      std::optional<fir::Instr> store;
      u32 store_bb = 0;
      bool ok = true;
      for (auto b : loop.body_nodes) {
        for (auto instr : cfg.bbrs[b].bb->instructions) {
          if (instr == load) {
            continue;
          }
          auto a = classify_access(instr);
          if (!a) {
            continue;
          }
          if (a->kind == MemAccess::Kind::Store && !a->ordered &&
              instr->args[0] == ptr) {
            if (store || instr->args[1].get_type() != load.result_type()) {
              ok = false;
            }
            store = instr;
            store_bb = b;
            continue;
          }
          if (touches(aq, instr, *a, load, access->loc, false, span)) {
            ok = false;
          }
        }
      }
      if (!ok || !store) {
        continue;
      }
      if (store_bb == loop.head &&
          header->get_instr_id(*store) < header->get_instr_id(load)) {
        continue;
      }
      if (!std::ranges::all_of(
              loop.tails, [&](u32 t) { return dom.dominates(store_bb, t); })) {
        continue;
      }

      fir::ValueR init{};
      if (guarded) {
        // the load only runs when the loop is entered, so it goes on a new
        // block on the entry edge
        auto pre_term = pre->get_terminator();
        const u16 edge = pre_term.get_bb_id(header);
        TVec<fir::ValueR> old_args{pre_term->bbs[edge].args.begin(),
                                   pre_term->bbs[edge].args.end()};
        fir::Builder nb{header};
        auto n = nb.append_bb();
        nb.at_end(n);
        init = nb.build_load(load.result_type(), ptr, false, false);
        auto br = nb.build_branch(header);
        for (auto old : old_args) {
          br.add_bb_arg(0, old);
        }
        pre_term.replace_bb(edge, n, false);
        cfg_changed = true;
        br.add_bb_arg(0, init);
      } else {
        fir::Builder bld{pre->get_terminator()};
        init = bld.build_load(load.result_type(), ptr, false, false);
      }
      auto acc = header.add_arg(
          ctx->storage.insert_bb_arg(header, load.result_type()));
      auto value = (*store)->args[1];

      TVec<u32> pred_ids;
      for (auto p : preds) {
        if (!std::ranges::contains(pred_ids, p)) {
          pred_ids.push_back(p);
        }
      }
      for (auto p : pred_ids) {
        auto term = cfg.bbrs[p].bb->get_terminator();
        for (u16 i = 0; i < term->bbs.size(); i++) {
          if (term->bbs[i].bb == header) {
            term.add_bb_arg(i, in_loop(p) ? value : init);
          }
        }
      }
      load.result().replace_all_uses(fir::ValueR{acc});
      load.destroy();
      utils::StatCollector::get().addi(1, "LoopMemPromote",
                                       utils::StatCollector::StatFOptim);
      return true;
    }
    return false;
  }

  // a header load through a pointer argument that is stepped by a select
  // (p = c ? x : p, the argmin of std::min_element) becomes a value argument
  // v = c ? *x : v, so the load leaves the loop carried chain. The loop has to
  // be read only and already load x every iteration before the select.
  bool forward_load_through_select(fir::Context &ctx, CFG &cfg, Dominators &dom,
                                   const LoopInfo &loop, bool &cfg_changed) {
    auto in_loop = [&](u32 id) { return loop.contains(id); };
    auto &preds = cfg.bbrs[loop.head].pred;
    if (!in_loop(loop.head) || loop.tails.size() != 1 ||
        preds.size() != loop.tails.size() + 1 || has_sideeffects(cfg, loop)) {
      return false;
    }
    auto header = cfg.bbrs[loop.head].bb;
    fir::Instr pre_term{fir::Instr::invalid()};
    u16 pre_edge = 0;
    for (auto p : preds) {
      if (in_loop(p)) {
        continue;
      }
      auto term = cfg.bbrs[p].bb->get_terminator();
      for (u16 i = 0; i < term->bbs.size(); i++) {
        if (term->bbs[i].bb == header) {
          if (pre_term.is_valid()) {
            return false;
          }
          pre_term = term;
          pre_edge = i;
        }
      }
    }
    if (!pre_term.is_valid()) {
      return false;
    }
    const u32 tail_id = loop.tails[0];
    auto tail_term = cfg.bbrs[tail_id].bb->get_terminator();
    // the edge into the header the pointer argument is stepped on
    std::optional<u16> back_edge;
    for (u16 i = 0; i < tail_term->bbs.size(); i++) {
      if (tail_term->bbs[i].bb == header) {
        if (back_edge) {
          return false;
        }
        back_edge = i;
      }
    }
    if (!back_edge) {
      return false;
    }

    for (u32 ai = 0; ai < header->args.size(); ai++) {
      const fir::ValueR ptr{header->args[ai]};
      if (!ptr.get_type()->is_ptr()) {
        continue;
      }
      const auto step = tail_term->bbs[*back_edge].args[ai];
      if (!step.is_instr() ||
          !step.as_instr()->is(fir::InstrType::SelectInstr)) {
        continue;
      }
      auto sel = step.as_instr();
      const bool ptr_true = sel->args[1] == ptr;
      if (!ptr_true && sel->args[2] != ptr) {
        continue;
      }
      const auto other = sel->args[ptr_true ? 2 : 1];
      if (other == ptr || !other.get_type()->is_ptr()) {
        continue;
      }
      const u32 sel_bb = cfg.get_bb_id(sel->get_parent());
      if (!in_loop(sel_bb) || !dom.dominates(sel_bb, tail_id)) {
        continue;
      }

      for (auto load : header->instructions) {
        if (!load->is(fir::InstrType::LoadInstr) || load->args[0] != ptr) {
          continue;
        }
        auto access = classify_access(load);
        if (!access || access->ordered || access->loc.size == 0) {
          continue;
        }
        std::optional<fir::Instr> other_load;
        for (auto b : loop.body_nodes) {
          for (auto cand : cfg.bbrs[b].bb->instructions) {
            if (!cand->is(fir::InstrType::LoadInstr) ||
                cand->args[0] != other ||
                cand.result_type() != load.result_type()) {
              continue;
            }
            auto ca = classify_access(cand);
            if (!ca || ca->ordered || ca->loc.size != access->loc.size) {
              continue;
            }
            const bool before = b == sel_bb
                                    ? sel->get_parent()->get_instr_id(cand) <
                                          sel->get_parent()->get_instr_id(sel)
                                    : dom.dominates(b, sel_bb);
            if (before) {
              other_load = cand;
            }
          }
        }
        if (!other_load) {
          continue;
        }

        auto init_ptr = pre_term->bbs[pre_edge].args[ai];
        auto acc = header.add_arg(
            ctx->storage.insert_bb_arg(header, load.result_type()));
        if (pre_term->is(fir::InstrType::BranchInstr)) {
          fir::Builder init_bld{pre_term};
          auto init =
              init_bld.build_load(load.result_type(), init_ptr, false, false);
          pre_term.add_bb_arg(pre_edge, init);
        } else {
          // the load only runs when the loop is entered, so it goes on a new
          // block on the entry edge
          TVec<fir::ValueR> old_args{pre_term->bbs[pre_edge].args.begin(),
                                     pre_term->bbs[pre_edge].args.end()};
          fir::Builder nb{header};
          auto n = nb.append_bb();
          nb.at_end(n);
          auto init = nb.build_load(load.result_type(), init_ptr, false, false);
          auto br = nb.build_branch(header);
          for (auto old : old_args) {
            br.add_bb_arg(0, old);
          }
          br.add_bb_arg(0, init);
          pre_term.replace_bb(pre_edge, n, false);
          cfg_changed = true;
        }
        fir::Builder bld{sel};
        auto next =
            !ptr_true
                ? bld.build_select(load.result_type(), sel->args[0],
                                   other_load->result(), fir::ValueR{acc})
                : bld.build_select(load.result_type(), sel->args[0],
                                   fir::ValueR{acc}, other_load->result());
        tail_term.add_bb_arg(*back_edge, next);
        load.result().replace_all_uses(fir::ValueR{acc});
        load.destroy();
        utils::StatCollector::get().addi(1, "LoopLoadForward",
                                         utils::StatCollector::StatFOptim);
        return true;
      }
    }
    return false;
  }

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedNC("LoopSimplify", COLOR_OPTIMF);
    CFG &cfg = AnalysisManager::cfg(func);
    Dominators &dom = AnalysisManager::dom(func);
    LoopInfoAnalysis loops{dom};

    bool modified = false;
    TSet<u64> no_change;
    size_t loop_ip1 = 1;
    // eliminate all loops that dont have sideffects
    //  TODO: handle if its values are used after and their result value is
    //  known/can be calculated withuot loop
    for (; loop_ip1 <= loops.info.size(); loop_ip1++) {
      // one call promotes one accumulator, it adds header arguments and maybe
      // a block on the entry edge (then the cfg is refreshed). Jammed lanes
      // are several accumulators in one loop, keep going until none is left
      bool fwd_cfg_changed = false;
      // the driver restarts after a change, a step that found nothing for an
      // unchanged loop finds nothing again
      auto step = [&](u64 id, auto &&fn) {
        const u64 key = loop_fingerprint(cfg, loops.info.at(loop_ip1 - 1)) ^
                        (id * UINT64_C(0x9E3779B97F4A7C15));
        if (no_change.contains(key)) {
          return false;
        }
        if (fn()) {
          return true;
        }
        no_change.insert(key);
        return false;
      };
      while (step(1, [&] {
        return promote_memory_accumulator(
            ctx, func, cfg, dom, loops.info.at(loop_ip1 - 1), fwd_cfg_changed);
      })) {
        modified = true;
        if (fwd_cfg_changed) {
          break;
        }
      }
      if (fwd_cfg_changed) {
        cfg.update(func, false);
        dom.update(cfg);
        loops.update(dom);
        loop_ip1 = 0;
        continue;
      }
      while (step(2, [&] {
        return forward_load_through_select(
            ctx, cfg, dom, loops.info.at(loop_ip1 - 1), fwd_cfg_changed);
      })) {
        modified = true;
        if (fwd_cfg_changed) {
          break;
        }
      }
      if (fwd_cfg_changed) {
        cfg.update(func, false);
        dom.update(cfg);
        loops.update(dom);
        loop_ip1 = 0;
        continue;
      }
      if (step(3,
               [&] {
                 return propagate_to_after_loop(cfg, dom, loops, loop_ip1 - 1);
               }) ||
          step(4, [&] {
            return merge_same_diret_induct_var(cfg, loops, loop_ip1 - 1);
          })) {
        modified = true;
        // loop_ip1 = 0;
        // continue;
      }
      if (merge_redundant_header_args(cfg, loops.info.at(loop_ip1 - 1))) {
        modified = true;
      }
      if (sink_store_to_exits(func, cfg, dom, loops.info.at(loop_ip1 - 1)) ||
          dead_loop_elimination(cfg, loops, loop_ip1 - 1) ||
          const_exit_value(ctx, cfg, dom, loops, loop_ip1 - 1,
                           no_change)) {
        modified = true;
        cfg.update(func, false);
        dom.update(cfg);
        loops.update(dom);
        loop_ip1 = 0;
        continue;
      }
    }
    return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
  }
};
}  // namespace foptim::optim
