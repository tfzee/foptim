#include <algorithm>
#include <bit>

#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/mem/block_mem_access.hpp"
#include "optim/func_passes/vectorizer/loop.hpp"
#include "optim/func_passes/vectorizer/recipes.hpp"

namespace foptim::optim::vec {

namespace {
using BinSub = fir::BinaryInstrSubType;

bool is_reduction_sub(BinSub sub, bool assoc_float) {
  switch (sub) {
    case BinSub::IntAdd:
    case BinSub::IntMul:
    case BinSub::And:
    case BinSub::Or:
    case BinSub::Xor:
      return true;
    case BinSub::FloatAdd:
    case BinSub::FloatMul:
      return assoc_float;
    default:
      return false;
  }
}

bool is_int_minmax(fir::IntrinsicSubType sub) {
  return sub == fir::IntrinsicSubType::SMin ||
         sub == fir::IntrinsicSubType::SMax ||
         sub == fir::IntrinsicSubType::UMin ||
         sub == fir::IntrinsicSubType::UMax;
}

// The backend has no horizontal multiply (and no 512 bit integer add). The
// other operations are lowered by halving the vector, which exists for 32 and
// 64 bit lanes in 128 and 256 bit vectors.
bool reduction_legal(const PlanNode &n, u32 bits, const Features &features) {
  const u32 elem = n.type->get_bitwidth();
  const bool halving = (elem == 32 || elem == 64) && bits >= 128 && bits <= 256;
  switch (n.red) {
    case RedKind::Add:
      return !(n.type->is_int() && bits > 256);
    case RedKind::And:
    case RedKind::Or:
    case RedKind::Xor:
      return halving;
    case RedKind::SMin:
    case RedKind::SMax:
    case RedKind::UMin:
    case RedKind::UMax:
      // 64 bit integer min and max need avx512vl
      return halving && (elem == 32 || (features.avx512f && features.avx512vl));
    case RedKind::Mul:
      return false;
  }
  return false;
}

bool loop_legal(const Plan &plan, u32 vf, const Features &features) {
  const u32 elem_bits = plan.loop.elem_bits;
  if (elem_bits != 8 && elem_bits != 16 && elem_bits != 32 && elem_bits != 64) {
    return false;
  }
  const u32 bits = vf * elem_bits;
  // no 512 bit byte or word vectors, InstSimplify widens byte vectors <= 16
  if (elem_bits < 32 && bits > 256) {
    return false;
  }
  if (elem_bits == 8 && vf < 32) {
    return false;
  }
  if (vf < 2 || !std::has_single_bit(vf) || bits < 128 ||
      bits > max_vector_bits(features)) {
    return false;
  }
  for (const auto &n : plan.nodes) {
    if (!is_vector_shape(n.shape)) {
      continue;
    }
    if (n.kind == NodeKind::Reduction) {
      if (!reduction_legal(n, bits, features)) {
        return false;
      }
      continue;
    }
    if (!node_legal(n, LaneSpace::LoopIter, vf, features)) {
      return false;
    }
    if (n.kind == NodeKind::Phi && !mask_width_ok(elem_bits, vf)) {
      return false;
    }
    if ((n.kind == NodeKind::Load || n.kind == NodeKind::Store) &&
        plan.loop.blocks[n.block].mask_bits != 0 &&
        !mask_width_ok(plan.loop.blocks[n.block].mask_bits, vf)) {
      return false;
    }
  }
  return true;
}

// Estimated saving of the whole loop for vf lanes (scalar cost of all
// iterations minus the vector loop, the setup and the scalar remainder).
i64 loop_benefit(const Plan &plan, u32 vf, const Features &features,
                 u32 assumed_trip) {
  const auto &l = plan.loop;
  const i64 trip =
      l.const_trip ? static_cast<i64>(*l.const_trip) : assumed_trip;
  const i64 s = l.scalar_cost;
  const i64 scalar_total = trip * s;
  i64 body = 0;
  for (const auto &n : plan.nodes) {
    body += node_vec_cost(n, vf, features);
    // masked accesses are a bit slower than plain ones
    if ((n.kind == NodeKind::Load || n.kind == NodeKind::Store) &&
        l.blocks[n.block].mask_bits != 0) {
      body += 1;
    }
  }
  body += 3 + l.mask_ops;
  const i64 vtrip = trip / vf * vf;
  const i64 rest = trip - vtrip;
  i64 overhead = 8 + l.n_reductions * 2 * std::countr_zero(vf);
  return scalar_total - (overhead + (vtrip / vf) * body + rest * s);
}

constexpr size_t MaxLoopBlocks = 16;

// Two values that compute the same thing: the same value or instructions of
// the same kind on the same operands (both paths of a branch computed i + 1).
bool same_scalar(fir::ValueR a, fir::ValueR b, u32 depth) {
  if (a == b) {
    return true;
  }
  if (depth == 0 || !a.is_instr() || !b.is_instr()) {
    return false;
  }
  auto x = a.as_instr();
  auto y = b.as_instr();
  if (x->instr_type != y->instr_type || x->subtype != y->subtype ||
      x->args.size() != y->args.size() || x.is_multi_result() ||
      x->has_pot_sideeffects() || x->pot_reads_mem() ||
      x->result_type() != y->result_type()) {
    return false;
  }
  for (size_t i = 0; i < x->args.size(); i++) {
    if (!same_scalar(x->args[i], y->args[i], depth - 1)) {
      return false;
    }
  }
  return true;
}

// The body blocks in reverse post order with the edges between them. The body
// has to be acyclic apart from the back edge and may only be left by the latch.
bool collect_blocks(CFG &cfg, const LoopInfo &loop, LoopData &l) {
  if (loop.tails.size() != 1 || loop.body_nodes.size() > MaxLoopBlocks) {
    return false;
  }
  const u32 head = loop.head;
  const u32 tail = loop.tails[0];
  auto in_body = [&](u32 node) {
    return std::ranges::find(loop.body_nodes, node) != loop.body_nodes.end();
  };
  TVec<u32> order;
  TMap<u32, u8> state;
  bool ok = true;
  auto visit = [&](auto &&self, u32 node) -> void {
    state[node] = 1;
    for (u32 succ : cfg.bbrs[node].succ) {
      if (succ == head || !in_body(succ)) {
        // only the latch may leave the body or close the loop
        ok &= node == tail;
        continue;
      }
      const auto it = state.find(succ);
      if (it == state.end()) {
        self(self, succ);
      } else if (it->second == 1) {
        ok = false;
      }
    }
    state[node] = 2;
    order.push_back(node);
  };
  visit(visit, head);
  if (!ok || order.size() != loop.body_nodes.size() || order.back() != head) {
    return false;
  }
  std::ranges::reverse(order);
  if (order.back() != tail) {
    return false;
  }
  TMap<u32, u32> idx_of;
  for (u32 i = 0; i < order.size(); i++) {
    idx_of[order[i]] = i;
    l.blocks.emplace_back();
    l.blocks.back().bb = cfg.bbrs[order[i]].bb;
  }
  l.header = l.blocks.front().bb;
  l.latch = l.blocks.back().bb;

  const u32 n = static_cast<u32>(order.size());
  TVec<u32> succ_mask(n, 0);
  for (u32 p = 0; p < n; p++) {
    auto term = l.blocks[p].bb->get_terminator();
    if (!term->is(fir::InstrType::BranchInstr) &&
        !term->is(fir::InstrType::CondBranchInstr)) {
      return false;
    }
    for (u32 slot = 0; slot < term->bbs.size(); slot++) {
      const auto target = term->bbs[slot].bb;
      const u32 id = cfg.get_bb_id(target);
      if (id == head || !in_body(id)) {
        continue;
      }
      const u32 t = idx_of.at(id);
      l.blocks[t].in_edges.push_back({.from = p, .slot = slot});
      succ_mask[p] |= 1U << t;
    }
  }

  // dominators and post dominators of the acyclic body
  TVec<u32> dom(n, 0);
  TVec<u32> pdom(n, 0);
  dom[0] = 1U;
  l.blocks[0].always = true;
  for (u32 b = 1; b < n; b++) {
    u32 d = ~0U;
    for (const auto &e : l.blocks[b].in_edges) {
      d &= dom[e.from];
    }
    dom[b] = d | (1U << b);
  }
  pdom[n - 1] = 1U << (n - 1);
  for (u32 b = n - 1; b > 0; b--) {
    const u32 i = b - 1;
    u32 d = ~0U;
    for (u32 t = 0; t < n; t++) {
      if ((succ_mask[i] >> t) & 1U) {
        d &= pdom[t];
      }
    }
    pdom[i] = d | (1U << i);
  }
  for (u32 b = 1; b < n; b++) {
    // closest dominator, the one with the most dominators itself
    u32 idom = 0;
    for (u32 d = 0; d < b; d++) {
      if (((dom[b] >> d) & 1U) != 0 &&
          std::popcount(dom[d]) >= std::popcount(dom[idom])) {
        idom = d;
      }
    }
    if (((pdom[idom] >> b) & 1U) != 0) {
      l.blocks[b].same_mask_as = idom;
      l.blocks[b].always = l.blocks[idom].always;
    }
  }
  return true;
}

// scalar_cost of the body: blocks that do not run in every iteration count half
void count_scalar_cost(LoopData &l) {
  u32 cost = 0;
  for (const auto &b : l.blocks) {
    const u32 n = static_cast<u32>(b.bb->instructions.size()) - 1;
    cost += b.mask_bits != 0 ? (n + 1) / 2 : n;
  }
  l.scalar_cost = cost;
}

bool build_loop_plan(PlanEnv &env, LoopInfo &loop, ScalarEvo &evo, Plan &plan) {
  auto &ctx = env.ctx;
  auto &cfg = *env.cfg;
  auto &l = plan.loop;
  if (!collect_blocks(cfg, loop, l)) {
    return false;
  }
  auto hdr = l.header;
  auto latch = l.latch;
  auto term = latch->get_terminator();
  if (!term->is(fir::InstrType::CondBranchInstr) || term->bbs.size() != 2) {
    return false;
  }
  if (term->bbs[0].bb == hdr && term->bbs[1].bb == hdr) {
    return false;
  }
  l.back_edge = term->bbs[0].bb == hdr ? 0 : 1;
  l.exit_edge = 1 - l.back_edge;
  if (term->bbs[l.back_edge].bb != hdr) {
    return false;
  }
  const auto &back_args = term->bbs[l.back_edge].args;
  const auto &exit_args = term->bbs[l.exit_edge].args;

  TripCountAnalysis tc;
  if (!tc.update(cfg, *env.dom, loop, evo) || tc.exit_iter == InvalidSCEV ||
      tc.from_bb != latch || !evo.is_expandable(tc.exit_iter)) {
    return false;
  }
  auto entry = evo.entry_block();
  if (!entry.is_valid()) {
    return false;
  }
  if (tc.cmp_rec != InvalidSCEV && (tc.cmp_signed || tc.cmp_unsigned)) {
    evo.assume_no_wrap(tc.cmp_rec, tc.cmp_signed);
  }
  {
    auto eterm = entry->get_terminator();
    u32 edges = 0;
    for (const auto &e : eterm->bbs) {
      edges += e.bb == hdr ? 1 : 0;
    }
    if (edges != 1) {
      return false;
    }
  }
  l.trip = tc.exit_iter;
  if (tc.const_exit_iter) {
    l.const_trip = *tc.const_exit_iter + 1;
  }
  const u32 trip_bw = evo.expr(tc.exit_iter).bw;

  LoopCtx mc{plan, evo};
  const bool assoc_float = ctx.config->optim.fltOpt.associative_math;

  auto block_of = [&](fir::BasicBlock bb) -> std::optional<u32> {
    for (u32 i = 0; i < l.blocks.size(); i++) {
      if (l.blocks[i].bb == bb) {
        return i;
      }
    }
    return std::nullopt;
  };

  // header arguments are induction variables or reductions
  bool have_counter = false;
  for (u32 k = 0; k < hdr->args.size(); k++) {
    fir::ValueR av{hdr->args[k]};
    const auto ty = av.get_type();
    if (!elem_type_ok(ty) && !ty->is_ptr()) {
      return false;
    }
    PlanNode n;
    n.value = av;
    n.type = ty;
    n.arg_id = k;
    const auto r = evo.get(av);
    if ((ty->is_int() || ty->is_ptr()) && evo.is_addrec(r) &&
        evo.is_const(evo.expr(r).b)) {
      n.kind = NodeKind::Induction;
      n.shape = LaneShape::Strided;
      n.stride = static_cast<i64>(evo.expr(evo.expr(r).b).cval_i());
      if (n.stride == 1 && !have_counter && ty->is_int() &&
          ty->get_bitwidth() == trip_bw) {
        have_counter = true;
        l.counter = k;
      }
    } else {
      // A reduction is acc2 = acc op x and nothing else touches acc or acc2.
      // The update may be conditional: a select of acc2 and acc, or a block
      // argument that takes either one of them.
      if (!env.config.reductions) {
        return false;
      }
      auto update_of = [&](fir::ValueR v) -> fir::Instr {
        if (!v.is_instr() || !block_of(v.as_instr()->get_parent())) {
          return fir::Instr{fir::Instr::invalid()};
        }
        auto i = v.as_instr();
        bool ok = false;
        if (i->is(fir::InstrType::BinaryInstr)) {
          ok = is_reduction_sub(static_cast<BinSub>(i->subtype), assoc_float);
        } else if (i->is(fir::InstrType::Intrinsic)) {
          ok = is_int_minmax(static_cast<fir::IntrinsicSubType>(i->subtype));
        }
        if (!ok || i->args.size() != 2 ||
            (i->args[0] == av) == (i->args[1] == av)) {
          return fir::Instr{fir::Instr::invalid()};
        }
        return i;
      };
      auto only_term_edge = [&](const auto &u, fir::BasicBlock dest, u32 arg) {
        const auto t = u.user;
        return u.type == fir::UseType::BBArg &&
               (t->is(fir::InstrType::BranchInstr) ||
                t->is(fir::InstrType::CondBranchInstr)) &&
               t->bbs[u.argId].bb == dest && u.bbArgId == arg;
      };
      // Whether x is greater or smaller than the accumulator decides if the
      // accumulator takes x, which makes it a minimum or a maximum
      auto minmax_kind_of = [&](fir::Instr c, fir::ValueR x,
                                bool x_on_true) -> std::optional<RedKind> {
        if (!c->is(fir::InstrType::ICmp)) {
          return std::nullopt;
        }
        using P = fir::ICmpInstrSubType;
        auto pred = static_cast<P>(c->subtype);
        const bool x_first = c->args[0] == x && c->args[1] == av;
        const bool acc_first = c->args[0] == av && c->args[1] == x;
        if (!x_first && !acc_first) {
          return std::nullopt;
        }
        auto swap = [](P p) {
          switch (p) {
            case P::SGT:
              return P::SLT;
            case P::SGE:
              return P::SLE;
            case P::SLT:
              return P::SGT;
            case P::SLE:
              return P::SGE;
            case P::UGT:
              return P::ULT;
            case P::UGE:
              return P::ULE;
            case P::ULT:
              return P::UGT;
            case P::ULE:
              return P::UGE;
            default:
              return p;
          }
        };
        auto negate = [](P p) {
          switch (p) {
            case P::SGT:
              return P::SLE;
            case P::SGE:
              return P::SLT;
            case P::SLT:
              return P::SGE;
            case P::SLE:
              return P::SGT;
            case P::UGT:
              return P::ULE;
            case P::UGE:
              return P::ULT;
            case P::ULT:
              return P::UGE;
            case P::ULE:
              return P::UGT;
            default:
              return p;
          }
        };
        if (acc_first) {
          pred = swap(pred);
        }
        if (!x_on_true) {
          pred = negate(pred);
        }
        switch (pred) {
          case P::SGT:
          case P::SGE:
            return RedKind::SMax;
          case P::SLT:
          case P::SLE:
            return RedKind::SMin;
          case P::UGT:
          case P::UGE:
            return RedKind::UMax;
          case P::ULT:
          case P::ULE:
            return RedKind::UMin;
          default:
            return std::nullopt;
        }
      };
      enum class Form { Direct, Select, Phi, PhiMinMax, SelectMinMax };
      Form form = Form::Direct;
      auto next = fir::Instr{fir::Instr::invalid()};
      fir::Instr sel{fir::Instr::invalid()};
      fir::BasicBlock join{fir::BasicBlock::invalid()};
      u32 join_arg = 0;
      fir::Instr minmax_cmp{fir::Instr::invalid()};
      RedKind minmax_kind = RedKind::Add;
      const auto back = back_args[k];
      if (back.is_instr() && back.as_instr()->is(fir::InstrType::SelectInstr)) {
        sel = back.as_instr();
        const auto sb = block_of(sel->get_parent());
        if (!sb || !l.blocks[*sb].always) {
          return false;
        }
        if (sel->args[2] == av) {
          next = update_of(sel->args[1]);
        } else if (sel->args[1] == av) {
          next = update_of(sel->args[2]);
        }
        form = Form::Select;
        if (!next.is_valid() && sel->args[0].is_instr() &&
            (sel->args[1] == av || sel->args[2] == av)) {
          const auto other = sel->args[1] == av ? sel->args[2] : sel->args[1];
          if (const auto kind = minmax_kind_of(sel->args[0].as_instr(), other,
                                               sel->args[1] == other)) {
            form = Form::SelectMinMax;
            minmax_cmp = sel->args[0].as_instr();
            minmax_kind = *kind;
          }
        }
      } else if (back.is_bb_arg()) {
        join = back.as_bb_arg()->get_parent();
        const auto jb = block_of(join);
        if (!jb || *jb == 0 || !l.blocks[*jb].always ||
            l.blocks[*jb].in_edges.size() != 2) {
          return false;
        }
        for (u32 j = 0; j < join->args.size(); j++) {
          if (fir::ValueR{join->args[j]} == back) {
            join_arg = j;
          }
        }
        const auto &in = l.blocks[*jb].in_edges;
        const auto v0 = l.blocks[in[0].from]
                            .bb->get_terminator()
                            ->bbs[in[0].slot]
                            .args[join_arg];
        const auto v1 = l.blocks[in[1].from]
                            .bb->get_terminator()
                            ->bbs[in[1].slot]
                            .args[join_arg];
        if (v0 == av) {
          next = update_of(v1);
        } else if (v1 == av) {
          next = update_of(v0);
        }
        form = Form::Phi;
        if (!next.is_valid()) {
          // m = x > m ? x : m, the edge that passes x is the one that is taken
          // when x compares greater (or smaller) than the accumulator
          const auto other = v0 == av ? v1 : v0;
          const auto &edge_x = v0 == av ? in[1] : in[0];
          const auto &edge_acc = v0 == av ? in[0] : in[1];
          if (v0 == av || v1 == av) {
            auto controlling = [&](LoopEdge e) -> std::optional<LoopEdge> {
              for (u32 hops = 0; hops < 3; hops++) {
                const auto &from = l.blocks[e.from];
                if (from.bb->get_terminator()->is(
                        fir::InstrType::CondBranchInstr)) {
                  return e;
                }
                if (from.in_edges.size() != 1) {
                  return std::nullopt;
                }
                e = from.in_edges[0];
              }
              return std::nullopt;
            };
            const auto cx = controlling(edge_x);
            const auto ca = controlling(edge_acc);
            if (cx && ca && cx->from == ca->from && cx->slot != ca->slot &&
                l.blocks[cx->from].always) {
              auto ct = l.blocks[cx->from].bb->get_terminator();
              if (ct->args[0].is_instr() &&
                  ct->args[0].as_instr()->is(fir::InstrType::ICmp)) {
                auto c = ct->args[0].as_instr();
                if (const auto kind = minmax_kind_of(c, other, cx->slot == 0)) {
                  form = Form::PhiMinMax;
                  minmax_cmp = c;
                  minmax_kind = *kind;
                }
              }
            }
          }
        }
      } else {
        next = update_of(back);
        const auto nb =
            next.is_valid() ? block_of(next->get_parent()) : std::nullopt;
        if (!nb || !l.blocks[*nb].always) {
          return false;
        }
      }
      if (!next.is_valid() && form != Form::PhiMinMax &&
          form != Form::SelectMinMax) {
        return false;
      }
      for (const auto &u : av.as_bb_arg()->uses) {
        const bool ok =
            (next.is_valid() && u.user == next) ||
            ((form == Form::Select || form == Form::SelectMinMax) &&
             u.user == sel) ||
            ((form == Form::PhiMinMax || form == Form::SelectMinMax) &&
             u.user == minmax_cmp) ||
            ((form == Form::Phi || form == Form::PhiMinMax) &&
             only_term_edge(u, join, join_arg));
        if (!ok) {
          return false;
        }
      }
      // the users of the combined value
      TVec<fir::Use> next_uses;
      if (next.is_valid()) {
        next_uses.assign(next->uses.begin(), next->uses.end());
      }
      for (const auto &u : next_uses) {
        bool ok = false;
        switch (form) {
          case Form::Direct:
            // back edge: only into the argument it is the accumulator of
            ok = u.user == term && u.type == fir::UseType::BBArg &&
                 !(u.argId == l.back_edge && u.bbArgId != k);
            break;
          case Form::Select:
          case Form::SelectMinMax:
            ok = u.user == sel;
            break;
          case Form::Phi:
          case Form::PhiMinMax:
            ok = only_term_edge(u, join, join_arg);
            break;
        }
        if (!ok) {
          return false;
        }
      }
      if (form != Form::Direct) {
        // the new accumulator goes around the loop and out of it
        const auto &uses = form == Form::Select || form == Form::SelectMinMax
                               ? sel->uses
                               : back.as_bb_arg()->uses;
        for (const auto &u : uses) {
          if (u.user != term || u.type != fir::UseType::BBArg ||
              (u.argId == l.back_edge && u.bbArgId != k)) {
            return false;
          }
        }
      }
      n.kind = NodeKind::Reduction;
      n.shape = LaneShape::Recurrence;
      if (form == Form::PhiMinMax || form == Form::SelectMinMax) {
        n.red = minmax_kind;
      } else if (next->is(fir::InstrType::Intrinsic)) {
        switch (static_cast<fir::IntrinsicSubType>(next->subtype)) {
          case fir::IntrinsicSubType::SMin:
            n.red = RedKind::SMin;
            break;
          case fir::IntrinsicSubType::SMax:
            n.red = RedKind::SMax;
            break;
          case fir::IntrinsicSubType::UMin:
            n.red = RedKind::UMin;
            break;
          default:
            n.red = RedKind::UMax;
            break;
        }
      } else {
        switch (static_cast<BinSub>(next->subtype)) {
          case BinSub::IntMul:
          case BinSub::FloatMul:
            n.red = RedKind::Mul;
            break;
          case BinSub::And:
            n.red = RedKind::And;
            break;
          case BinSub::Or:
            n.red = RedKind::Or;
            break;
          case BinSub::Xor:
            n.red = RedKind::Xor;
            break;
          default:
            n.red = RedKind::Add;
            break;
        }
      }
      l.n_reductions++;
    }
    l.node_of[av] = static_cast<u32>(plan.nodes.size());
    plan.nodes.push_back(std::move(n));
  }
  if (!have_counter) {
    return false;
  }

  // the lane width of the compares that the condition of an edge is made of
  auto edge_cond_bits = [&](const LoopBlock &from) -> std::optional<u32> {
    auto t = from.bb->get_terminator();
    if (!t->is(fir::InstrType::CondBranchInstr)) {
      return from.mask_bits;
    }
    const auto c = t->args[0];
    const u32 cb = mask_bits_of(c);
    if (cb == 0 || !is_vector_shape(shape_of(mc, c)) ||
        (from.mask_bits != 0 && from.mask_bits != cb)) {
      return std::nullopt;
    }
    return cb;
  };

  u32 n_instrs_total = 0;
  for (u32 bi = 0; bi < l.blocks.size(); bi++) {
    auto &blk = l.blocks[bi];
    auto bb = blk.bb;
    // the mask of the block
    if (!blk.always && blk.same_mask_as != ~0U) {
      blk.mask_bits = l.blocks[blk.same_mask_as].mask_bits;
    } else if (!blk.always) {
      u32 bits = 0;
      for (const auto &e : blk.in_edges) {
        const auto eb = edge_cond_bits(l.blocks[e.from]);
        // an edge that takes every lane would make the block unmasked
        if (!eb || *eb == 0 || (bits != 0 && bits != *eb)) {
          return false;
        }
        bits = *eb;
      }
      if (bits == 0) {
        return false;
      }
      blk.mask_bits = bits;
      l.mask_ops += 2 * static_cast<u32>(blk.in_edges.size());
    }
    // Block arguments of the blocks behind a branch. All edges passing the
    // same scalar (an induction step both paths computed) leave it a scalar,
    // otherwise the lanes are blended per edge.
    if (bi != 0) {
      for (u32 k = 0; k < bb->args.size(); k++) {
        fir::ValueR av{bb->args[k]};
        const auto ty = av.get_type();
        if (blk.in_edges.size() < 2) {
          return false;
        }
        auto edge_arg = [&](const LoopEdge &e) {
          return l.blocks[e.from].bb->get_terminator()->bbs[e.slot].args[k];
        };
        const auto first = edge_arg(blk.in_edges[0]);
        bool same = shape_of(mc, first) != LaneShape::Varying &&
                    shape_of(mc, first) != LaneShape::Recurrence;
        for (const auto &e : blk.in_edges) {
          same = same && shape_of(mc, edge_arg(e)) != LaneShape::Varying &&
                 shape_of(mc, edge_arg(e)) != LaneShape::Recurrence &&
                 same_scalar(edge_arg(e), first, 3);
        }
        PlanNode n;
        n.kind = NodeKind::Phi;
        n.value = av;
        n.type = ty;
        n.arg_id = k;
        n.block = bi;
        if (same) {
          n.shape = shape_of(mc, first);
          if (n.shape == LaneShape::Uniform &&
              !evo.is_invariant(evo.get(first))) {
            n.shape = LaneShape::Strided;
          }
        } else {
          if (!elem_type_ok(ty)) {
            return false;
          }
          for (const auto &e : blk.in_edges) {
            if (shape_of(mc, edge_arg(e)) == LaneShape::Strided) {
              return false;
            }
            // every edge but the last needs a mask of the width of the data
            const auto eb = edge_cond_bits(l.blocks[e.from]);
            if (!eb || *eb != ty->get_bitwidth()) {
              return false;
            }
          }
          n.shape = LaneShape::Varying;
        }
        l.node_of[av] = static_cast<u32>(plan.nodes.size());
        plan.nodes.push_back(std::move(n));
      }
    }
    BlockMemAccesses mem{bb};
    if (mem.n_barriers() != 0) {
      return false;
    }
    const auto n_instrs = bb->instructions.size();
    n_instrs_total += static_cast<u32>(n_instrs) - 1;
    for (size_t idx = 0; idx + 1 < n_instrs; idx++) {
      auto instr = bb->instructions[idx];
      fir::ValueR iv = instr.result();
      PlanNode n;
      n.value = iv;
      n.block = bi;
      const Recipe *recipe = find_recipe(instr->instr_type);
      const bool has_loop_recipe = recipe != nullptr && recipe->match_loop;
      bool vector_operand = false;
      for (auto a : instr->args) {
        vector_operand |= is_vector_shape(shape_of(mc, a));
      }
      if (instr->is(fir::InstrType::LoadInstr) ||
          instr->is(fir::InstrType::StoreInstr)) {
        if (!has_loop_recipe || !recipe->match_loop(mc, instr, n)) {
          return false;
        }
        auto a = mem.index_of(instr);
        if (!a) {
          return false;
        }
        if (blk.mask_bits != 0 &&
            (blk.mask_bits != n.type->get_bitwidth() ||
             (blk.mask_bits != 32 && blk.mask_bits != 64))) {
          // only 32 and 64 bit lanes have masked loads and stores
          return false;
        }
        l.accesses.push_back(
            {.instr = instr,
             .addr = evo.get(instr->args[0]),
             .size = n.type->get_size(),
             .is_store = instr->is(fir::InstrType::StoreInstr)});
        l.n_vector_ops++;
      } else if (vector_operand) {
        if (!has_loop_recipe || !recipe->match_loop(mc, instr, n)) {
          return false;
        }
        l.n_vector_ops++;
      } else {
        // computes the value of lane 0 (addresses, induction math), copied
        if (instr->has_pot_sideeffects() || instr->pot_reads_mem() ||
            instr->result_type()->is_vec()) {
          return false;
        }
        n.kind = NodeKind::Scalar;
        n.type = instr->result_type();
        n.shape = evo.is_invariant(evo.get(iv)) ? LaneShape::Uniform
                                                : LaneShape::Strided;
        for (size_t i = instr->args.size(); i > 0; i--) {
          const u32 o = shape_node(mc, instr->args[i - 1]);
          if (o != ~0U) {
            n.children.push_back(o);
          }
        }
        l.n_scalar_ops++;
      }
      l.node_of[iv] = static_cast<u32>(plan.nodes.size());
      plan.nodes.push_back(std::move(n));
    }
  }
  (void)n_instrs_total;
  count_scalar_cost(l);
  if (l.n_vector_ops == 0 || l.accesses.empty()) {
    return false;
  }

  // all vector values need the same element width
  for (const auto &n : plan.nodes) {
    if (!is_vector_shape(n.shape)) {
      continue;
    }
    // a mask has the width of the lanes that were compared
    if (n.kind != NodeKind::Cmp && n.type->is_int() &&
        n.type->get_bitwidth() == 1) {
      continue;
    }
    const u32 bits = n.kind == NodeKind::Cmp ? n.op_type->get_bitwidth()
                                             : n.type->get_bitwidth();
    if (l.elem_bits == 0) {
      l.elem_bits = bits;
    } else if (l.elem_bits != bits) {
      return false;
    }
  }

  // check the uses outside of the loop and of the vector values
  auto check_uses = [&](fir::ValueR v, const PlanNode *n) {
    const auto &uses = v.is_instr() ? v.as_instr()->uses : v.as_bb_arg()->uses;
    const bool vec = n != nullptr && is_vector_shape(n->shape) &&
                     n->kind != NodeKind::Reduction;
    for (const auto &u : uses) {
      if (!block_of(u.user->get_parent())) {
        return false;
      }
      if (u.user->get_parent() != latch || u.user != term) {
        if (!vec || !(u.user->is(fir::InstrType::BranchInstr) ||
                      u.user->is(fir::InstrType::CondBranchInstr))) {
          continue;
        }
        // a vector value that is passed on by a branch has to end up in a
        // blended block argument, or be the condition (a mask)
        if (u.type == fir::UseType::BBArg) {
          const auto dest = u.user->bbs[u.argId].bb;
          const auto di = block_of(dest);
          if (!di || *di == 0) {
            return false;
          }
        }
        continue;
      }
      if (vec) {
        // vectors leave the latch only as the next value of a reduction
        bool is_next = false;
        for (u32 k = 0; k < back_args.size(); k++) {
          is_next |=
              plan.nodes[k].kind == NodeKind::Reduction && back_args[k] == v;
        }
        if (!is_next && u.type == fir::UseType::BBArg) {
          return false;
        }
      }
    }
    return true;
  };
  for (const auto &blk : l.blocks) {
    for (auto a : blk.bb->args) {
      const auto it = l.node_of.find(fir::ValueR{a});
      if (!check_uses(fir::ValueR{a}, it == l.node_of.end()
                                          ? nullptr
                                          : &plan.nodes[it->second])) {
        return false;
      }
    }
    const auto n_instrs = blk.bb->instructions.size();
    for (size_t idx = 0; idx + 1 < n_instrs; idx++) {
      auto instr = blk.bb->instructions[idx];
      if (!check_uses(instr.result(),
                      &plan.nodes[l.node_of.at(instr.result())])) {
        return false;
      }
    }
  }
  // values passed to the code after the loop are loop invariant or the next
  // value of an induction variable or reduction
  for (const auto &e : exit_args) {
    bool ok = !(e.is_instr() && block_of(e.as_instr()->get_parent())) &&
              !(e.is_bb_arg() && block_of(e.as_bb_arg()->get_parent()));
    for (u32 k = 0; k < back_args.size() && !ok; k++) {
      const auto &kn = plan.nodes[k];
      ok = e == back_args[k] &&
           (kn.kind == NodeKind::Induction || kn.kind == NodeKind::Reduction);
    }
    if (!ok) {
      return false;
    }
  }
  for (u32 k = 0; k < back_args.size(); k++) {
    if (plan.nodes[k].kind == NodeKind::Reduction &&
        !l.node_of.contains(back_args[k])) {
      return false;
    }
  }
  return true;
}

class LoopSeedProvider final : public SeedProvider {
 public:
  void scan(SeedRegion &r) final {
    if (r.loop == nullptr || !r.config.loops) {
      return;
    }
    // cheap precheck, the planner looks at the shape again
    if (r.loop->body_nodes.size() > MaxLoopBlocks ||
        r.loop->tails.size() != 1) {
      return;
    }
    Seed s;
    s.kind = SeedKind::Loop;
    s.loop = r.loop;
    seeds.push_back(std::move(s));
  }
  void finalize(const conf::CompConf & /*conf*/) final {}

  bool plan(PlanEnv &env, Seed &seed, Plan &plan) final {
    const auto &features = env.ctx.config->target.features;
    plan.space = LaneSpace::LoopIter;
    evos.push_back(std::make_unique<ScalarEvo>(*env.cfg, *seed.loop));
    ScalarEvo &evo = *evos.back();
    plan.evo = &evo;
    if (!build_loop_plan(env, *seed.loop, evo, plan)) {
      return false;
    }
    u32 max_vf = max_vector_bits(features) / plan.loop.elem_bits;
    if (env.config.max_vf != 0) {
      max_vf = std::min(max_vf, env.config.max_vf);
    }
    u32 best_vf = 0;
    i64 best_benefit = 0;
    for (u32 vf = max_vf; vf >= 2; vf /= 2) {
      if (!loop_legal(plan, vf, features)) {
        continue;
      }
      LoopMemDep dep{evo, env.aa};
      bool deps_ok = true;
      const auto &acc = plan.loop.accesses;
      for (size_t i = 0; i < acc.size() && deps_ok; i++) {
        for (size_t j = i + 1; j < acc.size() && deps_ok; j++) {
          deps_ok = dep.query(acc[i], acc[j], vf) == MemDep::Independent;
        }
      }
      if (!deps_ok) {
        continue;
      }
      const i64 benefit =
          loop_benefit(plan, vf, features, env.config.assumed_trip);
      if (benefit <= 0) {
        continue;
      }
      if (best_vf == 0 || benefit > best_benefit) {
        best_vf = vf;
        best_benefit = benefit;
      }
    }
    if (best_vf == 0) {
      return false;
    }
    plan.vf = best_vf;
    plan.benefit = best_benefit;
    return true;
  }

 private:
  // plans point into these
  std::vector<std::unique_ptr<ScalarEvo>> evos;
};
}  // namespace

u32 max_vector_bits(const Features &features) {
  return features.avx512f ? 512 : 256;
}

std::unique_ptr<SeedProvider> make_loop_seeds() {
  return std::make_unique<LoopSeedProvider>();
}

bool route_exit_uses(fir::Context &ctx, CFG &cfg, LoopInfo &loop) {
  if (loop.body_nodes.size() != 1 || loop.tails.size() != 1 ||
      loop.head != loop.tails[0]) {
    return false;
  }
  auto hdr = cfg.bbrs[loop.head].bb;
  auto term = hdr->get_terminator();
  if (!term->is(fir::InstrType::CondBranchInstr) || term->bbs.size() != 2) {
    return false;
  }
  const u16 exit_edge = term->bbs[0].bb == hdr ? 1 : 0;
  auto exit_bb = term->bbs[exit_edge].bb;
  if (exit_bb == hdr || term->bbs[1 - exit_edge].bb != hdr ||
      cfg.bbrs[cfg.get_bb_id(exit_bb)].pred.size() != 1) {
    return false;
  }
  bool changed = false;
  const size_t n_instrs = hdr->instructions.size();
  for (size_t idx = 0; idx + 1 < n_instrs; idx++) {
    auto instr = hdr->instructions[idx];
    if (instr.is_multi_result() || instr.result_type()->is_void()) {
      continue;
    }
    bool used_outside = false;
    for (const auto &u : instr->uses) {
      used_outside |= u.user->get_parent() != hdr;
    }
    if (!used_outside) {
      continue;
    }
    auto arg = ctx->storage.insert_bb_arg(
        fir::BBArgumentData{exit_bb, instr.result_type()});
    exit_bb.add_arg(arg);
    term.add_bb_arg(exit_edge, instr.result());
    instr.result().replace_uses_outside_block(hdr, fir::ValueR{arg});
    changed = true;
  }
  return changed;
}

}  // namespace foptim::optim::vec
