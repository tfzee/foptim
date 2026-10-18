#include "loop_analysis.hpp"

#include <fmt/core.h>

#include <algorithm>
#include <array>

#include "ir/basic_block.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "utils/bitset.hpp"
#include "utils/set.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

void LoopInfoAnalysis::update(Dominators &dom) { update_impl(dom); }
void LoopInfoAnalysis::update(DominatorTree &dom) { update_impl(dom); }

template <class DomType>
void LoopInfoAnalysis::update_impl(DomType &dom) {
  info.clear();

  const CFG &cfg = *dom.cfg;
  TVec<u32> deq{};
  deq.reserve(32);

  utils::BitSet backward{cfg.bbrs.size(), false};

  // dominance holds vacuously for unreachable blocks, so they must not become
  // part of a loop body
  utils::BitSet reachable{cfg.bbrs.size(), false};
  if (!cfg.bbrs.empty()) {
    reachable[cfg.entry].set(true);
    deq.push_back(cfg.entry);
    while (!deq.empty()) {
      u32 curr = deq.back();
      deq.pop_back();
      for (auto succ : cfg.bbrs[curr].succ) {
        if (!reachable[succ]) {
          reachable[succ].set(true);
          deq.push_back(succ);
        }
      }
    }
  }

  // if a bb has a incoming edge from a block that it dominates we found a loop
  // header
  TVec<u32> tails;
  tails.reserve(32);
  for (u32 bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
    tails.clear();
    for (auto pred : cfg.bbrs[bb_id].pred) {
      if (dom.dominates(bb_id, pred) &&
          std::ranges::find(tails, pred) == tails.end()) {
        tails.push_back(pred);
      }
    }
    // we found an loop header + tails
    if (tails.empty()) {
      continue;
    }

    backward.reset(false);
    deq.clear();

    TVec<u32> body_nodes;
    body_nodes.reserve(2 + (tails.size() * 2));

    for (auto tail : tails) {
      backward[tail].set(true);
      body_nodes.push_back(tail);
      deq.push_back(tail);
    }

    // if one of the potential body nodes is not dominated by the header
    //  we do not consider this a real loop even though there are
    //  backwards edges
    // bool invalid_loop = false;

    while (!deq.empty()) {
      u32 curr = deq.back();
      deq.pop_back();
      for (auto pred : cfg.bbrs[curr].pred) {
        if (reachable[pred] && !backward[pred] && dom.dominates(bb_id, pred)) {
          backward[pred].set(true);
          // if () {
          //   fmt::println(" INVALID {}", pred);
          //   invalid_loop = true;
          //   break;
          // }
          body_nodes.push_back(pred);
          deq.push_back(pred);
        }
      }
      // if (invalid_loop) {
      //   break;
      // }
    }
    // if (invalid_loop) {
    //   fmt::println("  INVALID");
    //   continue;
    // }

    TVec<u32> leaving_nodes;
    leaving_nodes.reserve(body_nodes.size() / 2);
    for (u32 node : body_nodes) {
      for (auto succ : cfg.bbrs[node].succ) {
        if (std::ranges::find(body_nodes, succ) == body_nodes.end()) {
          leaving_nodes.push_back(node);
        }
      }
    }

    info.push_back({
        bb_id,
        tails,
        std::move(body_nodes),
        std::move(leaving_nodes),
    });
  }
}

void LoopInfo::dump() const {
  fmt::println("Loop\n  Header: {}\n  Body: {}\n  Tails: {}\n  Leaving: {}\n",
               head, body_nodes, tails, leaving_nodes);
}
void LoopInfoAnalysis::dump() const {
  for (const auto &loop : info) {
    loop.dump();
  }
}

void LoopRangeAnalysis::dump() const {
  fmt::print("{}  ", induction_var);
  if (known_lower) {
    fmt::print("{}", lower_bound_var);
    fmt::print("@{}", lower_bound);
  }
  fmt::print("..");
  if (known_upper) {
    fmt::print("{}", upper_bound_var);
    fmt::print("@{}", upper_bound);
  }
  fmt::println(" step: {}", a);
}

void LoopRangeAnalysis::reset() {
  lower_bound = 0;
  upper_bound = 0;
  a = 0;
  induction_var = fir::BBArgument{fir::BBArgument::invalid()};
  type = IterationType::PlusA;
  known_lower = false;
  known_upper = false;
}

bool LoopRangeAnalysis::update(CFG &cfg, LoopInfo &info) {
  reset();
  fir::BasicBlock head = cfg.bbrs[info.head].bb;
  constexpr bool debug_print = false;
  // info.dump();
  // need 1 induction var
  if (head->args.empty()) {
    // just gonna assume the first for now
    if constexpr (debug_print) {
      fmt::println("0a");
    }
    return false;
  }
  // the bounds are tracked as i128 numbers, only exact for narrow counters
  if (const auto ity = fir::ValueR(head->args[0]).get_type();
      ity->is_int() && ity->get_bitwidth() > 64) {
    return false;
  }
  // only 1 incoming edge into the loop
  if (cfg.bbrs[info.head].pred.size() != 1 + info.tails.size()) {
    if constexpr (debug_print) {
      fmt::println("0");
    }
    return false;
  }
  for (u32 p : cfg.bbrs[info.head].pred) {
    if (std::ranges::find(info.body_nodes, p) == info.body_nodes.end()) {
      auto incoming_bb_term = cfg.bbrs[p].bb->get_terminator();
      auto outgoing_id = incoming_bb_term.get_bb_id(head);
      auto arg = incoming_bb_term->bbs[outgoing_id].args[0];
      if (arg.is_constant() && arg.as_constant()->is_int()) {
        known_lower = true;
        lower_bound = arg.as_constant()->as_int();
        lower_bound_var = fir::Use::bb_arg(incoming_bb_term, outgoing_id, 0);
      }
      break;
    }
  }

  auto induct_incoming = head->args[0];
  induction_var = induct_incoming;

  bool set = false;
  i128 constant_off = 0;
  fir::ValueR induct_outgoing = fir::ValueR();

  for (auto tail : info.tails) {
    auto tail_bb = cfg.bbrs[tail].bb;
    auto term = tail_bb->get_terminator();
    if (tail_bb != head || !term->is(fir::InstrType::CondBranchInstr)) {
      if constexpr (debug_print) {
        fmt::println("1");
      }
      return false;
    }
    // there can only be 1 arg
    fir::ValueR induction_arg;
    if (term->bbs[0].bb == head) {
      induction_arg = term->bbs[0].args[0];
    } else {
      induction_arg = term->bbs[1].args[0];
    }
    // TODO: improve
    if (!induction_arg.is_instr()) {
      if constexpr (debug_print) {
        fmt::println("2");
      }
      return false;
    }
    auto induct_oper = induction_arg.as_instr();
    if (!induct_oper->is(fir::BinaryInstrSubType::IntAdd) &&
        !induct_oper->is(fir::BinaryInstrSubType::PtrAdd)) {
      if constexpr (debug_print) {
        fmt::println("3");
      }
      return false;
    }

    u8 var_index = 0;
    u8 constant_index = 0;
    if (induct_oper->args[0].is_constant()) {
      var_index = 1;
      constant_index = 0;
    } else if (induct_oper->args[1].is_constant()) {
      var_index = 0;
      constant_index = 1;
    } else {
      if constexpr (debug_print) {
        fmt::println("4a");
      }
      return false;
    }
    auto con = induct_oper->args[constant_index].as_constant();
    auto var = induct_oper->args[var_index];
    if (!con->is_int()) {
      if constexpr (debug_print) {
        fmt::println("4");
      }
      return false;
    }
    if (!var.is_bb_arg() || var.as_bb_arg() != induct_incoming) {
      if constexpr (debug_print) {
        fmt::println("5");
      }
      return false;
    }

    auto new_off = con->as_int();
    if (set && constant_off != new_off) {
      if constexpr (debug_print) {
        fmt::println("6");
      }
      return false;
    }

    constant_off = new_off;
    induct_outgoing = induction_arg;
    set = true;
  }
  a = constant_off;

  // find the upper bound
  {
    auto term = head->get_terminator();
    ASSERT(term->is(fir::InstrType::CondBranchInstr));
    auto cond = term->args[0];

    bool continue_on_true = false;
    for (u32 node_idx : info.body_nodes) {
      if (cfg.bbrs[node_idx].bb == term->bbs[0].bb) {
        continue_on_true = true;
        break;
      }
    }

    if (cond.is_instr()) {
      auto c_instr = cond.as_instr();
      if (c_instr->is(fir::BinaryInstrSubType::Xor) &&
          c_instr->args.size() == 2) {
        if (c_instr->args[1].is_constant() &&
            c_instr->args[1].as_constant()->is_int() &&
            c_instr->args[1].as_constant()->as_int() == 1) {
          continue_on_true = !continue_on_true;
          cond = c_instr->args[0];
        } else if (c_instr->args[0].is_constant() &&
                   c_instr->args[0].as_constant()->is_int() &&
                   c_instr->args[0].as_constant()->as_int() == 1) {
          continue_on_true = !continue_on_true;
          cond = c_instr->args[1];
        }
      } else if (c_instr->is(fir::UnaryInstrSubType::Not)) {
        continue_on_true = !continue_on_true;
        cond = c_instr->args[0];
      }
    }

    if (!cond.is_instr() || !cond.as_instr()->is(fir::InstrType::ICmp)) {
      if constexpr (debug_print) {
        fmt::println("7");
      }
      return false;
    }

    auto cond_instr = cond.as_instr();
    auto condi = static_cast<fir::ICmpInstrSubType>(cond_instr->subtype);

    u8 var_index = 0;
    u8 constant_index = 0;
    if (cond_instr->args[0].is_constant()) {
      var_index = 1;
      constant_index = 0;
    } else if (cond_instr->args[1].is_constant()) {
      var_index = 0;
      constant_index = 1;
    } else {
      if constexpr (debug_print) {
        fmt::println("9a");
      }
      return false;
    }

    fir::ICmpInstrSubType normalized_condi = condi;
    if (constant_index == 0) {
      switch (condi) {
        case fir::ICmpInstrSubType::ULT:
          normalized_condi = fir::ICmpInstrSubType::UGT;
          break;
        case fir::ICmpInstrSubType::SLT:
          normalized_condi = fir::ICmpInstrSubType::SGT;
          break;
        case fir::ICmpInstrSubType::UGT:
          normalized_condi = fir::ICmpInstrSubType::ULT;
          break;
        case fir::ICmpInstrSubType::SGT:
          normalized_condi = fir::ICmpInstrSubType::SLT;
          break;
        case fir::ICmpInstrSubType::ULE:
          normalized_condi = fir::ICmpInstrSubType::UGE;
          break;
        case fir::ICmpInstrSubType::SLE:
          normalized_condi = fir::ICmpInstrSubType::SGE;
          break;
        case fir::ICmpInstrSubType::UGE:
          normalized_condi = fir::ICmpInstrSubType::ULE;
          break;
        case fir::ICmpInstrSubType::SGE:
          normalized_condi = fir::ICmpInstrSubType::SLE;
          break;
        default:
          break;  // EQ / NE stay the same
      }
    }

    bool is_valid_bound = false;
    i128 bound_offset = 0;

    if (continue_on_true) {
      switch (normalized_condi) {
        case fir::ICmpInstrSubType::ULT:
        case fir::ICmpInstrSubType::SLT:
        case fir::ICmpInstrSubType::NE:
          is_valid_bound = true;
          bound_offset = 0;
          break;
        case fir::ICmpInstrSubType::ULE:
        case fir::ICmpInstrSubType::SLE:
          is_valid_bound = true;
          bound_offset = 1;
          break;

        default:
          break;
      }
    } else {
      switch (normalized_condi) {
        case fir::ICmpInstrSubType::UGE:
        case fir::ICmpInstrSubType::SGE:
        case fir::ICmpInstrSubType::EQ:
          is_valid_bound = true;
          bound_offset = 0;
          break;
        case fir::ICmpInstrSubType::UGT:
        case fir::ICmpInstrSubType::SGT:
          is_valid_bound = true;
          bound_offset = 1;
          break;

        default:
          break;
      }
    }

    if (!is_valid_bound) {
      if constexpr (debug_print) {
        fmt::println("8");
      }
      return false;
    }

    auto con = cond_instr->args[constant_index].as_constant();
    auto var = cond_instr->args[var_index];
    if (!con->is_int()) {
      if constexpr (debug_print) {
        fmt::println("9");
      }
      return false;
    }
    if (var != induct_outgoing) {
      if constexpr (debug_print) {
        fmt::println("10");
      }
      return false;
    }

    known_upper = true;
    upper_bound = con->as_int() + bound_offset;
    upper_bound_var = fir::Use::norm(cond_instr, constant_index);
  }
  if (known_lower && known_upper) {
    if (a == 0 || (a > 0 && lower_bound > upper_bound) ||
        (a < 0 && lower_bound < upper_bound)) {
      if constexpr (debug_print) {
        fmt::println("11 bad direction: lower={} upper={} step={}", lower_bound,
                     upper_bound, a);
      }
      return false;
    }
  }

  if constexpr (debug_print) {
    fmt::println("f");
  }

  return true;
}

namespace {
using ICmpSub = fir::ICmpInstrSubType;

// Interpret `v` as a `bw` bit integer.
i128 wrap_int(i128 v, u32 bw, bool is_signed) {
  if (bw == 0 || bw >= 128) {
    return v;
  }
  const u128 mask = (static_cast<u128>(1) << bw) - 1;
  const u128 u = static_cast<u128>(v) & mask;
  if (is_signed && ((u >> (bw - 1)) & 1) != 0) {
    return static_cast<i128>(u | ~mask);
  }
  return static_cast<i128>(u);
}

bool fits_int(i128 v, u32 bw, bool is_signed) {
  return wrap_int(v, bw, is_signed) == v;
}

// predicate that holds exactly when `p` does not
ICmpSub negate_pred(ICmpSub p) {
  switch (p) {
    case ICmpSub::SLT:
      return ICmpSub::SGE;
    case ICmpSub::ULT:
      return ICmpSub::UGE;
    case ICmpSub::SGT:
      return ICmpSub::SLE;
    case ICmpSub::UGT:
      return ICmpSub::ULE;
    case ICmpSub::SLE:
      return ICmpSub::SGT;
    case ICmpSub::ULE:
      return ICmpSub::UGT;
    case ICmpSub::SGE:
      return ICmpSub::SLT;
    case ICmpSub::UGE:
      return ICmpSub::ULT;
    case ICmpSub::NE:
      return ICmpSub::EQ;
    case ICmpSub::EQ:
      return ICmpSub::NE;
    default:
      return ICmpSub::INVALID;
  }
}

// predicate for swapped operands: `a p b` == `b swap(p) a`
ICmpSub swap_pred(ICmpSub p) {
  switch (p) {
    case ICmpSub::SLT:
      return ICmpSub::SGT;
    case ICmpSub::ULT:
      return ICmpSub::UGT;
    case ICmpSub::SGT:
      return ICmpSub::SLT;
    case ICmpSub::UGT:
      return ICmpSub::ULT;
    case ICmpSub::SLE:
      return ICmpSub::SGE;
    case ICmpSub::ULE:
      return ICmpSub::UGE;
    case ICmpSub::SGE:
      return ICmpSub::SLE;
    case ICmpSub::UGE:
      return ICmpSub::ULE;
    case ICmpSub::NE:
    case ICmpSub::EQ:
      return p;
    default:
      return ICmpSub::INVALID;
  }
}

bool pred_holds(ICmpSub p, i128 a, i128 b) {
  switch (p) {
    case ICmpSub::SLT:
    case ICmpSub::ULT:
      return a < b;
    case ICmpSub::SLE:
    case ICmpSub::ULE:
      return a <= b;
    case ICmpSub::SGT:
    case ICmpSub::UGT:
      return a > b;
    case ICmpSub::SGE:
    case ICmpSub::UGE:
      return a >= b;
    case ICmpSub::NE:
      return a != b;
    case ICmpSub::EQ:
      return a == b;
    default:
      return false;
  }
}

bool apint_pred_holds(ICmpSub p, const APInt &a, const APInt &b) {
  switch (p) {
    case ICmpSub::SLT:
      return a.slt(b);
    case ICmpSub::ULT:
      return a.ult(b);
    case ICmpSub::SLE:
      return a.sle(b);
    case ICmpSub::ULE:
      return a.ule(b);
    case ICmpSub::SGT:
      return a.sgt(b);
    case ICmpSub::UGT:
      return a.ugt(b);
    case ICmpSub::SGE:
      return a.sge(b);
    case ICmpSub::UGE:
      return a.uge(b);
    case ICmpSub::NE:
      return a != b;
    case ICmpSub::EQ:
      return a == b;
    default:
      return false;
  }
}

// num > 0, den > 0
i128 ceil_div(i128 num, i128 den) { return (num + den - 1) / den; }

// bit width of integers and pointers, 0 for everything else
u32 value_bw(fir::ValueR v) {
  auto ty = v.get_type();
  if (ty->is_int()) {
    return ty->get_bitwidth();
  }
  if (ty->is_ptr()) {
    return 64;
  }
  return 0;
}

i128 add_mod(i128 a, i128 b, u32 bw) {
  return wrap_int(
      static_cast<i128>(static_cast<u128>(a) + static_cast<u128>(b)), bw, true);
}

i128 mul_mod(i128 a, i128 b, u32 bw) {
  return wrap_int(
      static_cast<i128>(static_cast<u128>(a) * static_cast<u128>(b)), bw, true);
}

constexpr u32 MaxExprDepth = 128;

SCEVExpr node(SCEVExpr::Type t, bool invariant, u32 bw, APInt cval = APInt(),
              fir::ValueR val = {}, SCEVRef a = InvalidSCEV,
              SCEVRef b = InvalidSCEV, SCEVRef c = InvalidSCEV) {
  SCEVExpr e;
  e.t = t;
  e.invariant = invariant;
  e.bw = bw;
  e.cval = std::move(cval);
  e.val = val;
  e.a = a;
  e.b = b;
  e.c = c;
  return e;
}

bool cmp_is_signed(ICmpSub p) {
  return p != ICmpSub::ULT && p != ICmpSub::ULE && p != ICmpSub::UGT &&
         p != ICmpSub::UGE;
}
}  // namespace

SCEVRef ScalarEvo::push(const SCEVExpr &e) {
  using T = SCEVExpr::Type;
  u64 h = 0;
  const bool share = e.t != T::Unknown && e.t != T::Input;
  if (share) {
    auto mix = [&h](u64 x) {
      h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };
    mix(static_cast<u64>(e.t));
    mix(e.bw);
    mix(e.cval.hash());
    mix(e.pred);
    mix(TMap<fir::ValueR, int>::hasher{}(e.val));
    mix(e.a);
    mix(e.b);
    mix(e.c);
    for (SCEVRef cand : interned[h]) {
      const SCEVExpr &o = exprs[cand];
      if (o.t == e.t && o.bw == e.bw && o.pred == e.pred &&
          o.cval.bits() == e.cval.bits() && o.cval == e.cval &&
          o.val == e.val && o.a == e.a && o.b == e.b && o.c == e.c) {
        return cand;
      }
    }
  }
  exprs.push_back(e);
  const auto ref = static_cast<SCEVRef>(exprs.size() - 1);
  if (share) {
    interned[h].push_back(ref);
  }
  return ref;
}

SCEVRef ScalarEvo::mk_const(i128 v, u32 bw) {
  return mk_const(APInt::from_i128(bw, v));
}

SCEVRef ScalarEvo::mk_const(APInt v) {
  const u32 bw = v.bits();
  return push(node(SCEVExpr::Type::Const, true, bw, std::move(v)));
}

SCEVRef ScalarEvo::mk_invariant(fir::ValueR v) {
  return push(node(SCEVExpr::Type::Invariant, true, value_bw(v), APInt(), v));
}

SCEVRef ScalarEvo::mk_unknown(u32 bw) {
  return push(node(SCEVExpr::Type::Unknown, false, bw));
}

SCEVRef ScalarEvo::mk_addrec(SCEVRef start, SCEVRef step) {
  const SCEVExpr es = exprs[start];
  const SCEVExpr ec = exprs[step];
  ASSERT(es.invariant && ec.invariant);
  if (ec.t == SCEVExpr::Type::Const && ec.cval.is_zero()) {
    return start;
  }
  return push(
      node(SCEVExpr::Type::AddRec, false, es.bw, APInt(), {}, start, step));
}

SCEVRef ScalarEvo::mk_add(SCEVRef a, SCEVRef b) {
  SCEVExpr ea = exprs[a];
  SCEVExpr eb = exprs[b];
  const u32 bw = ea.bw;
  using T = SCEVExpr::Type;
  if (ea.t == T::Unknown || eb.t == T::Unknown || ea.bw != eb.bw) {
    return mk_unknown(bw);
  }
  if (ea.t == T::Const && eb.t == T::Const) {
    return mk_const(ea.cval + eb.cval);
  }
  // constants to the right
  if (ea.t == T::Const) {
    std::swap(a, b);
    std::swap(ea, eb);
  }
  if (eb.t == T::Const && eb.cval.is_zero()) {
    return a;
  }
  const bool rec_a = ea.t == T::AddRec;
  const bool rec_b = eb.t == T::AddRec;
  if (rec_a && rec_b) {
    return mk_addrec(mk_add(ea.a, eb.a), mk_add(ea.b, eb.b));
  }
  if (rec_a && eb.invariant) {
    return mk_addrec(mk_add(ea.a, b), ea.b);
  }
  if (rec_b && ea.invariant) {
    return mk_addrec(mk_add(a, eb.a), eb.b);
  }
  return push(
      node(T::Add, ea.invariant && eb.invariant, bw, APInt(), {}, a, b));
}

SCEVRef ScalarEvo::mk_mul(SCEVRef a, SCEVRef b) {
  SCEVExpr ea = exprs[a];
  SCEVExpr eb = exprs[b];
  const u32 bw = ea.bw;
  using T = SCEVExpr::Type;
  if (ea.t == T::Unknown || eb.t == T::Unknown || ea.bw != eb.bw) {
    return mk_unknown(bw);
  }
  if (ea.t == T::Const && eb.t == T::Const) {
    return mk_const(ea.cval * eb.cval);
  }
  if (ea.t == T::Const) {
    std::swap(a, b);
    std::swap(ea, eb);
  }
  if (eb.t == T::Const) {
    if (eb.cval.is_zero()) {
      return b;
    }
    if (eb.cval.is_one()) {
      return a;
    }
  }
  const bool rec_a = ea.t == T::AddRec;
  const bool rec_b = eb.t == T::AddRec;
  if (rec_a && eb.invariant) {
    return mk_addrec(mk_mul(ea.a, b), mk_mul(ea.b, b));
  }
  if (rec_b && ea.invariant) {
    return mk_addrec(mk_mul(a, eb.a), mk_mul(a, eb.b));
  }
  return push(
      node(T::Mul, ea.invariant && eb.invariant, bw, APInt(), {}, a, b));
}

SCEVRef ScalarEvo::mk_ext(SCEVRef a, bool is_signed, u32 bw) {
  const SCEVExpr ea = exprs[a];
  using T = SCEVExpr::Type;
  if (ea.t == T::Unknown || bw <= ea.bw) {
    return mk_unknown(bw);
  }
  if (ea.t == T::Const) {
    return mk_const(is_signed ? ea.cval.sext(bw) : ea.cval.zext(bw));
  }
  if (ea.t == T::AddRec && is_const(ea.b)) {
    for (const auto &nw : no_wrap) {
      if (nw.is_signed == is_signed && same_expr(nw.rec, a)) {
        return mk_addrec(mk_ext(ea.a, is_signed, bw), mk_ext(ea.b, true, bw));
      }
    }
  }
  return push(
      node(is_signed ? T::SExt : T::ZExt, ea.invariant, bw, APInt(), {}, a));
}

void ScalarEvo::assume_no_wrap(SCEVRef q, bool is_signed) {
  if (!is_addrec(q) || !is_const(exprs[q].b)) {
    return;
  }
  const SCEVExpr eq = exprs[q];
  no_wrap.push_back({q, is_signed});
  const SCEVRef pre =
      mk_addrec(mk_add(eq.a, mk_mul(eq.b, mk_const(-1, eq.bw))), eq.b);
  if (is_addrec(pre)) {
    no_wrap.push_back({pre, is_signed});
  }
  classify_values();
}

SCEVRef ScalarEvo::mk_trunc(SCEVRef a, u32 bw) {
  const SCEVExpr ea = exprs[a];
  using T = SCEVExpr::Type;
  if (ea.t == T::Unknown || bw >= ea.bw || bw == 0) {
    return mk_unknown(bw);
  }
  if (ea.t == T::Const) {
    return mk_const(ea.cval.trunc(bw));
  }
  return push(node(T::Trunc, ea.invariant, bw, APInt(), {}, a));
}

SCEVRef ScalarEvo::mk_resize(SCEVRef a, u32 bw, bool is_signed) {
  const u32 from = exprs[a].bw;
  if (from == bw) {
    return a;
  }
  return bw > from ? mk_ext(a, is_signed, bw) : mk_trunc(a, bw);
}

SCEVRef ScalarEvo::mk_max(SCEVRef a, SCEVRef b, bool is_signed) {
  const SCEVExpr ea = exprs[a];
  const SCEVExpr eb = exprs[b];
  const u32 bw = ea.bw;
  using T = SCEVExpr::Type;
  if (ea.t == T::Unknown || eb.t == T::Unknown || ea.bw != eb.bw) {
    return mk_unknown(bw);
  }
  if (ea.t == T::Const && eb.t == T::Const) {
    const bool b_greater =
        is_signed ? ea.cval.slt(eb.cval) : ea.cval.ult(eb.cval);
    return mk_const(b_greater ? eb.cval : ea.cval);
  }
  if (same_expr(a, b)) {
    return a;
  }
  return push(node(is_signed ? T::SMax : T::UMax, ea.invariant && eb.invariant,
                   bw, APInt(), {}, a, b));
}

SCEVRef ScalarEvo::mk_cmp(ICmpSub pred, SCEVRef a, SCEVRef b) {
  const SCEVExpr ea = exprs[a];
  const SCEVExpr eb = exprs[b];
  using T = SCEVExpr::Type;
  if (ea.t == T::Unknown || eb.t == T::Unknown || ea.bw != eb.bw ||
      pred == ICmpSub::INVALID) {
    return mk_unknown(1);
  }
  if (ea.t == T::Const && eb.t == T::Const) {
    return mk_const(APInt(1, u64{apint_pred_holds(pred, ea.cval, eb.cval)}));
  }
  auto cmp = node(T::Cmp, ea.invariant && eb.invariant, 1, APInt(), {}, a, b);
  cmp.pred = static_cast<u32>(pred);
  return push(cmp);
}

SCEVRef ScalarEvo::mk_select(SCEVRef cond, SCEVRef t, SCEVRef f) {
  const SCEVExpr ec = exprs[cond];
  const SCEVExpr et = exprs[t];
  const SCEVExpr ef = exprs[f];
  using T = SCEVExpr::Type;
  if (ec.t == T::Unknown || et.t == T::Unknown || ef.t == T::Unknown ||
      et.bw != ef.bw || ec.bw != 1) {
    return mk_unknown(et.bw);
  }
  if (ec.t == T::Const) {
    return !ec.cval.is_zero() ? t : f;
  }
  if (same_expr(t, f)) {
    return t;
  }
  return push(node(T::Select, ec.invariant && et.invariant && ef.invariant,
                   et.bw, APInt(), {}, cond, t, f));
}

SCEVRef ScalarEvo::at_iteration(SCEVRef r, SCEVRef k) {
  TMap<SCEVRef, SCEVRef> memo;
  auto rec = [&](auto &&self, SCEVRef x) -> SCEVRef {
    const SCEVExpr e = exprs[x];
    using T = SCEVExpr::Type;
    if (e.invariant) {
      return x;
    }
    if (auto it = memo.find(x); it != memo.end()) {
      return it->second;
    }
    SCEVRef res = InvalidSCEV;
    switch (e.t) {
      case T::AddRec:
        res = mk_add(e.a, mk_mul(mk_resize(k, e.bw, false), e.b));
        break;
      case T::Add:
      case T::Mul:
      case T::SMax:
      case T::UMax: {
        const SCEVRef l = self(self, e.a);
        const SCEVRef r2 = self(self, e.b);
        if (l != InvalidSCEV && r2 != InvalidSCEV) {
          res = e.t == T::Add   ? mk_add(l, r2)
                : e.t == T::Mul ? mk_mul(l, r2)
                                : mk_max(l, r2, e.t == T::SMax);
        }
        break;
      }
      case T::Cmp: {
        const SCEVRef l = self(self, e.a);
        const SCEVRef r2 = self(self, e.b);
        if (l != InvalidSCEV && r2 != InvalidSCEV) {
          res = mk_cmp(static_cast<ICmpSub>(e.pred), l, r2);
        }
        break;
      }
      case T::Select: {
        const SCEVRef cnd = self(self, e.a);
        const SCEVRef t = self(self, e.b);
        const SCEVRef f = self(self, e.c);
        if (cnd != InvalidSCEV && t != InvalidSCEV && f != InvalidSCEV) {
          res = mk_select(cnd, t, f);
        }
        break;
      }
      case T::SExt:
      case T::ZExt:
      case T::Trunc: {
        const SCEVRef l = self(self, e.a);
        if (l != InvalidSCEV) {
          res = e.t == T::Trunc ? mk_trunc(l, e.bw)
                                : mk_ext(l, e.t == T::SExt, e.bw);
        }
        break;
      }
      default:
        break;
    }
    if (res != InvalidSCEV && exprs[res].t == T::Unknown) {
      res = InvalidSCEV;
    }
    memo.insert({x, res});
    return res;
  };
  return rec(rec, r);
}

bool ScalarEvo::is_expandable(SCEVRef r) const {
  TMap<SCEVRef, bool> memo;
  auto rec = [&](auto &&self, SCEVRef x) -> bool {
    const SCEVExpr &e = exprs[x];
    using T = SCEVExpr::Type;
    if (e.bw == 0 || !e.invariant) {
      return false;
    }
    if (auto it = memo.find(x); it != memo.end()) {
      return it->second;
    }
    bool ok = false;
    switch (e.t) {
      case T::Const:
        ok = true;
        break;
      case T::Invariant:
        ok = e.val.get_type()->is_int();
        break;
      case T::Add:
      case T::Mul:
      case T::SMax:
      case T::UMax:
      case T::Cmp:
        ok = self(self, e.a) && self(self, e.b);
        break;
      case T::Select:
        ok = self(self, e.a) && self(self, e.b) && self(self, e.c);
        break;
      case T::SExt:
      case T::ZExt:
      case T::Trunc:
        ok = self(self, e.a);
        break;
      default:
        break;
    }
    memo.insert({x, ok});
    return ok;
  };
  return rec(rec, r);
}

bool ScalarEvo::is_non_negative(SCEVRef r) const {
  const SCEVExpr &e = exprs[r];
  using T = SCEVExpr::Type;
  switch (e.t) {
    case T::Const:
      return !e.cval.is_neg();
    case T::ZExt:
      return e.bw > exprs[e.a].bw;
    case T::SMax:
      return is_non_negative(e.a) || is_non_negative(e.b);
    case T::Select:
      return is_non_negative(e.b) && is_non_negative(e.c);
    default:
      return false;
  }
}

bool ScalarEvo::in_loop(fir::ValueR v) const {
  fir::BasicBlock parent{fir::BasicBlock::invalid()};
  if (v.is_instr()) {
    parent = v.as_instr()->get_parent();
  } else if (v.is_bb_arg()) {
    parent = v.as_bb_arg()->get_parent();
  } else {
    return false;
  }
  return body.contains(cfg->get_bb_id(parent));
}

SCEVRef ScalarEvo::get(fir::ValueR v) { return expr_of(v, 0); }

SCEVRef ScalarEvo::expr_of(fir::ValueR v, u32 depth) {
  if (auto it = lookup.find(v); it != lookup.end()) {
    return it->second;
  }
  const u32 bw = value_bw(v);
  SCEVRef r = InvalidSCEV;
  bool is_loop_val = false;
  if (v.is_constant()) {
    auto c = v.as_constant();
    if (!c->is_poison() && (c->is_int() || c->is_null())) {
      r = mk_const(c->as_int(), bw);
    } else {
      r = mk_invariant(v);
    }
  } else if (!v.is_instr() && !v.is_bb_arg()) {
    r = mk_unknown(bw);
  } else if (!in_loop(v)) {
    r = mk_invariant(v);
  } else {
    is_loop_val = true;
    r = mk_unknown(bw);
    if (v.is_bb_arg() && bw != 0 && depth < MaxExprDepth) {
      // provisional entry: a cycle through other blocks stays unknown
      lookup.insert({v, r});
      r = block_arg_expr(v, depth);
    } else if (v.is_instr() && bw != 0 && depth < MaxExprDepth) {
      auto instr = v.as_instr();
      using BT = fir::BinaryInstrSubType;
      if (instr->is(fir::InstrType::BinaryInstr)) {
        const auto sub = static_cast<BT>(instr->subtype);
        if (sub == BT::IntAdd || sub == BT::PtrAdd || sub == BT::IntSub ||
            sub == BT::IntMul) {
          // the offset of a ptr add has to be as wide as the pointer
          if (value_bw(instr->args[0]) == bw &&
              value_bw(instr->args[1]) == bw) {
            const SCEVRef lhs = expr_of(instr->args[0], depth + 1);
            const SCEVRef rhs = expr_of(instr->args[1], depth + 1);
            if (sub == BT::IntMul) {
              r = mk_mul(lhs, rhs);
            } else if (sub == BT::IntSub) {
              r = mk_add(lhs, mk_mul(rhs, mk_const(-1, bw)));
            } else {
              r = mk_add(lhs, rhs);
            }
          }
        }
      } else if (instr->is(fir::InstrType::ZExt) ||
                 instr->is(fir::InstrType::SExt)) {
        if (instr->args[0].get_type()->is_int()) {
          r = mk_ext(expr_of(instr->args[0], depth + 1),
                     instr->is(fir::InstrType::SExt), bw);
        }
      } else if (instr->is(fir::InstrType::ITrunc)) {
        if (instr->args[0].get_type()->is_int()) {
          r = mk_trunc(expr_of(instr->args[0], depth + 1), bw);
        }
      } else if (instr->is(fir::InstrType::SelectInstr)) {
        const SCEVRef m = select_as_max(instr, depth + 1);
        if (m != InvalidSCEV) {
          r = m;
        } else if (value_bw(instr->args[1]) == bw &&
                   value_bw(instr->args[2]) == bw &&
                   value_bw(instr->args[0]) == 1) {
          r = mk_select(expr_of(instr->args[0], depth + 1),
                        expr_of(instr->args[1], depth + 1),
                        expr_of(instr->args[2], depth + 1));
        }
      } else if (instr->is(fir::IntrinsicSubType::SMax) ||
                 instr->is(fir::IntrinsicSubType::UMax)) {
        if (value_bw(instr->args[0]) == bw && value_bw(instr->args[1]) == bw) {
          r = mk_max(expr_of(instr->args[0], depth + 1),
                     expr_of(instr->args[1], depth + 1),
                     instr->is(fir::IntrinsicSubType::SMax));
        }
      } else if (instr->is(fir::InstrType::ICmp)) {
        const u32 obw = value_bw(instr->args[0]);
        if (obw != 0 && value_bw(instr->args[1]) == obw) {
          r = mk_cmp(static_cast<ICmpSub>(instr->subtype),
                     expr_of(instr->args[0], depth + 1),
                     expr_of(instr->args[1], depth + 1));
        }
      }
    }
  }
  lookup[v] = r;
  if (is_loop_val) {
    loop_vals.push_back(v);
  }
  return r;
}

SCEVRef ScalarEvo::block_arg_expr(fir::ValueR v, u32 depth) {
  const u32 bw = value_bw(v);
  auto arg = v.as_bb_arg();
  auto bb = arg->get_parent();
  const u32 bb_id = cfg->get_bb_id(bb);
  size_t idx = 0;
  while (idx < bb->args.size() && !(bb->args[idx] == arg)) {
    idx++;
  }

  // the value on every incoming edge and the block it comes from
  struct Edge {
    u32 pred;
    fir::ValueR val;
  };
  TVec<Edge> edges;
  for (auto pred : cfg->bbrs[bb_id].pred) {
    auto term = cfg->bbrs[pred].bb->get_terminator();
    for (auto &target : term->bbs) {
      if (target.bb != bb) {
        continue;
      }
      // DCE leaves dead block arguments with invalid edge values
      if (idx >= target.args.size() || target.args[idx].is_invalid()) {
        return mk_unknown(bw);
      }
      edges.push_back({pred, target.args[idx]});
    }
  }
  if (edges.empty()) {
    return mk_unknown(bw);
  }

  // all edges agree
  SCEVRef merged = expr_of(edges[0].val, depth + 1);
  bool all_same = true;
  for (size_t i = 1; i < edges.size(); i++) {
    if (!same_expr(merged, expr_of(edges[i].val, depth + 1))) {
      all_same = false;
      break;
    }
  }
  if (all_same) {
    return merged;
  }

  // two edges that are told apart by one conditional branch (a triangle or a
  // diamond, what SimplifyCFG turns into a select): cond ? a : b
  if (edges.size() != 2 || edges[0].pred == edges[1].pred) {
    return mk_unknown(bw);
  }
  auto single_pred = [&](u32 id) {
    return cfg->bbrs[id].pred.size() == 1 ? cfg->bbrs[id].pred[0] : ~0U;
  };
  for (size_t x = 0; x < 2; x++) {
    const Edge &first = edges[x];
    const Edge &second = edges[1 - x];
    auto first_term = cfg->bbrs[first.pred].bb->get_terminator();
    // triangle: first branches to bb or to second, which only continues to bb
    if (first_term->is(fir::InstrType::CondBranchInstr) &&
        first_term->bbs.size() == 2 && single_pred(second.pred) == first.pred) {
      const bool bb_is_true = first_term->bbs[0].bb == bb;
      const fir::BasicBlock other = first_term->bbs[bb_is_true ? 1 : 0].bb;
      if (first_term->bbs[bb_is_true ? 0 : 1].bb == bb &&
          cfg->get_bb_id(other) == second.pred) {
        const SCEVRef cond = expr_of(first_term->args[0], depth + 1);
        const SCEVRef direct = expr_of(first.val, depth + 1);
        const SCEVRef via = expr_of(second.val, depth + 1);
        return bb_is_true ? mk_select(cond, direct, via)
                          : mk_select(cond, via, direct);
      }
    }
  }
  // diamond: both edges come from blocks that only continue to bb and are the
  // two targets of one conditional branch
  const u32 d = single_pred(edges[0].pred);
  if (d != ~0U && d == single_pred(edges[1].pred)) {
    auto d_term = cfg->bbrs[d].bb->get_terminator();
    if (d_term->is(fir::InstrType::CondBranchInstr) &&
        d_term->bbs.size() == 2) {
      const u32 t = cfg->get_bb_id(d_term->bbs[0].bb);
      const u32 f = cfg->get_bb_id(d_term->bbs[1].bb);
      for (size_t x = 0; x < 2; x++) {
        if (edges[x].pred == t && edges[1 - x].pred == f) {
          return mk_select(expr_of(d_term->args[0], depth + 1),
                           expr_of(edges[x].val, depth + 1),
                           expr_of(edges[1 - x].val, depth + 1));
        }
      }
    }
  }
  return mk_unknown(bw);
}

// `select(x > y, x, y)` (any spelling of the compare) is max(x, y)
SCEVRef ScalarEvo::select_as_max(fir::Instr sel, u32 depth) {
  auto cond = sel->args[0];
  if (!cond.is_instr() || !cond.as_instr()->is(fir::InstrType::ICmp)) {
    return InvalidSCEV;
  }
  auto cmp = cond.as_instr();
  auto pred = static_cast<ICmpSub>(cmp->subtype);
  auto x = cmp->args[0];
  auto y = cmp->args[1];
  if (pred == ICmpSub::SLT || pred == ICmpSub::SLE || pred == ICmpSub::ULT ||
      pred == ICmpSub::ULE) {
    std::swap(x, y);
    pred = swap_pred(pred);
  }
  const bool is_signed = pred == ICmpSub::SGT || pred == ICmpSub::SGE;
  if (!is_signed && pred != ICmpSub::UGT && pred != ICmpSub::UGE) {
    return InvalidSCEV;
  }
  // x > y ? x : y
  if (!(sel->args[1] == x && sel->args[2] == y)) {
    return InvalidSCEV;
  }
  return mk_max(expr_of(x, depth), expr_of(y, depth), is_signed);
}

bool ScalarEvo::same_expr(SCEVRef a, SCEVRef b) const {
  if (a == b) {
    return true;
  }
  const SCEVExpr &ea = exprs[a];
  const SCEVExpr &eb = exprs[b];
  if (ea.t != eb.t || ea.bw != eb.bw) {
    return false;
  }
  using T = SCEVExpr::Type;
  switch (ea.t) {
    case T::Const:
      return ea.cval == eb.cval;
    case T::Invariant:
      return ea.val == eb.val;
    case T::Cmp:
      return ea.pred == eb.pred && same_expr(ea.a, eb.a) &&
             same_expr(ea.b, eb.b);
    case T::Select:
      return same_expr(ea.a, eb.a) && same_expr(ea.b, eb.b) &&
             same_expr(ea.c, eb.c);
    case T::Add:
    case T::Mul:
    case T::SMax:
    case T::UMax:
    case T::AddRec:
      return same_expr(ea.a, eb.a) && same_expr(ea.b, eb.b);
    case T::SExt:
    case T::ZExt:
    case T::Trunc:
      return same_expr(ea.a, eb.a);
    case T::Unknown:
    case T::Input:
      return false;
  }
  return false;
}

SCEVRef ScalarEvo::extract_step(SCEVRef e, fir::ValueR h) {
  const SCEVExpr ex = exprs[e];
  using T = SCEVExpr::Type;
  if (ex.t == T::Input && ex.val == h) {
    return mk_const(0, ex.bw);
  }
  if (ex.t == T::Select && exprs[ex.a].invariant) {
    // c ? h + s1 : h + s2
    const SCEVRef s1 = extract_step(ex.b, h);
    const SCEVRef s2 = s1 == InvalidSCEV ? InvalidSCEV : extract_step(ex.c, h);
    return s2 == InvalidSCEV ? s2 : mk_select(ex.a, s1, s2);
  }
  if (ex.t != T::Add) {
    return InvalidSCEV;
  }
  const bool inv_a = exprs[ex.a].invariant;
  const bool inv_b = exprs[ex.b].invariant;
  if (!inv_a && inv_b) {
    const SCEVRef s = extract_step(ex.a, h);
    return s == InvalidSCEV ? s : mk_add(s, ex.b);
  }
  if (inv_a && !inv_b) {
    const SCEVRef s = extract_step(ex.b, h);
    return s == InvalidSCEV ? s : mk_add(s, ex.a);
  }
  return InvalidSCEV;
}

void ScalarEvo::update(CFG &cfg_, LoopInfo &info) {
  cfg = &cfg_;
  loop = &info;
  exprs.clear();
  interned.clear();
  lookup.clear();
  body.clear();
  loop_vals.clear();
  entry_bb = fir::BasicBlock{fir::BasicBlock::invalid()};
  for (auto node : info.body_nodes) {
    body.insert(node);
  }

  auto head = cfg->bbrs[info.head].bb;
  const size_t n_args = head->args.size();

  // incoming values of the header arguments: exactly one edge from outside is
  // needed to know the start value
  TVec<fir::ValueR> entering;
  TVec<TVec<fir::ValueR>> backs(n_args);
  u32 n_outside_edges = 0;
  TSet<u32> seen_preds;
  for (auto pred : cfg->bbrs[info.head].pred) {
    if (!seen_preds.insert(pred).second) {
      continue;
    }
    const bool inside = body.contains(pred);
    auto term = cfg->bbrs[pred].bb->get_terminator();
    for (auto &target : term->bbs) {
      if (target.bb != head || target.args.size() != n_args) {
        continue;
      }
      if (inside) {
        for (size_t i = 0; i < n_args; i++) {
          backs[i].push_back(target.args[i]);
        }
      } else {
        n_outside_edges++;
        entering.assign(target.args.begin(), target.args.end());
        entry_bb = cfg->bbrs[pred].bb;
      }
    }
  }
  const bool know_start = n_outside_edges == 1;
  if (!know_start) {
    entry_bb = fir::BasicBlock{fir::BasicBlock::invalid()};
  }

  // 1. classify the header arguments: with every header argument as an opaque
  //    input, the value on the back edge has to be `arg + invariant`
  for (size_t i = 0; i < n_args; i++) {
    const fir::ValueR arg{head->args[i]};
    lookup.insert({arg, push(node(SCEVExpr::Type::Input, false, value_bw(arg),
                                  APInt(), arg))});
  }
  rec_start.assign(n_args, InvalidSCEV);
  rec_step.assign(n_args, InvalidSCEV);
  no_wrap.clear();
  for (size_t i = 0; i < n_args; i++) {
    const fir::ValueR arg{head->args[i]};
    if (!know_start || value_bw(arg) == 0 || in_loop(entering[i])) {
      continue;
    }
    const SCEVRef start = expr_of(entering[i], 0);
    if (!exprs[start].invariant) {
      continue;
    }
    SCEVRef step = InvalidSCEV;
    bool ok = !backs[i].empty();
    for (auto back : backs[i]) {
      const SCEVRef st = extract_step(expr_of(back, 0), arg);
      if (st == InvalidSCEV || (step != InvalidSCEV && !same_expr(step, st))) {
        ok = false;
        break;
      }
      step = st;
    }
    if (ok) {
      rec_start[i] = start;
      rec_step[i] = step;
    }
  }

  classify_values();
}

void ScalarEvo::classify_values() {
  // 2. everything again with the header arguments replaced by what they are
  lookup.clear();
  loop_vals.clear();
  auto head = cfg->bbrs[loop->head].bb;
  const size_t n_args = head->args.size();
  for (size_t i = 0; i < n_args; i++) {
    const fir::ValueR arg{head->args[i]};
    const SCEVRef r = rec_start[i] != InvalidSCEV
                          ? mk_addrec(rec_start[i], rec_step[i])
                          : mk_unknown(value_bw(arg));
    lookup.insert({arg, r});
    loop_vals.push_back(arg);
  }
  for (auto node : loop->body_nodes) {
    auto bb = cfg->bbrs[node].bb;
    if (node != loop->head) {
      for (auto arg : bb->args) {
        expr_of(fir::ValueR{arg}, 0);
      }
    }
    for (auto instr : bb->instructions) {
      if (instr.is_multi_result()) {
        continue;
      }
      if (value_bw(instr.result()) != 0) {
        expr_of(instr.result(), 0);
      }
    }
  }
}

std::optional<i128> ScalarEvo::eval_at(SCEVRef r, i128 k) const {
  const SCEVExpr &e = exprs[r];
  using T = SCEVExpr::Type;
  if (e.t == T::Const) {
    return e.bw <= 128 ? std::optional<i128>(e.cval_i()) : std::nullopt;
  }
  if (e.bw == 0 || e.bw > 64) {
    return std::nullopt;
  }
  switch (e.t) {
    case T::AddRec: {
      auto s = eval_at(e.a, 0);
      auto c = eval_at(e.b, 0);
      if (!s || !c) {
        return std::nullopt;
      }
      return add_mod(*s, mul_mod(k, *c, e.bw), e.bw);
    }
    case T::Add:
    case T::Mul:
    case T::SMax:
    case T::UMax: {
      auto x = eval_at(e.a, k);
      auto y = eval_at(e.b, k);
      if (!x || !y) {
        return std::nullopt;
      }
      if (e.t == T::SMax || e.t == T::UMax) {
        const bool sgn = e.t == T::SMax;
        return wrap_int(
            std::max(wrap_int(*x, e.bw, sgn), wrap_int(*y, e.bw, sgn)), e.bw,
            true);
      }
      return e.t == T::Add ? add_mod(*x, *y, e.bw) : mul_mod(*x, *y, e.bw);
    }
    case T::Trunc: {
      auto x = eval_at(e.a, k);
      if (!x) {
        return std::nullopt;
      }
      return wrap_int(*x, e.bw, true);
    }
    case T::Cmp: {
      auto x = eval_at(e.a, k);
      auto y = eval_at(e.b, k);
      if (!x || !y) {
        return std::nullopt;
      }
      const auto pred = static_cast<ICmpSub>(e.pred);
      const bool sgn = cmp_is_signed(pred);
      const u32 obw = exprs[e.a].bw;
      return pred_holds(pred, wrap_int(*x, obw, sgn), wrap_int(*y, obw, sgn))
                 ? 1
                 : 0;
    }
    case T::Select: {
      auto c = eval_at(e.a, k);
      if (!c) {
        return std::nullopt;
      }
      return eval_at(*c != 0 ? e.b : e.c, k);
    }
    case T::SExt:
    case T::ZExt: {
      auto x = eval_at(e.a, k);
      if (!x) {
        return std::nullopt;
      }
      return wrap_int(wrap_int(*x, exprs[e.a].bw, e.t == T::SExt), e.bw, true);
    }
    default:
      return std::nullopt;
  }
}

void ScalarEvo::dump() const {
  using T = SCEVExpr::Type;
  fmt::println("Scalar evolution:");
  for (size_t i = 0; i < exprs.size(); ++i) {
    const auto &e = exprs[i];
    fmt::print("  e{} (i{}{}): ", i, e.bw, e.invariant ? ", inv" : "");
    switch (e.t) {
      case T::Unknown:
        fmt::println("?");
        break;
      case T::Const:
        fmt::println("{}", e.cval.to_string(true));
        break;
      case T::Invariant:
        fmt::println("invariant {}", e.val);
        break;
      case T::Input:
        fmt::println("input {}", e.val);
        break;
      case T::Add:
        fmt::println("e{} + e{}", e.a, e.b);
        break;
      case T::Mul:
        fmt::println("e{} * e{}", e.a, e.b);
        break;
      case T::SExt:
        fmt::println("sext e{}", e.a);
        break;
      case T::ZExt:
        fmt::println("zext e{}", e.a);
        break;
      case T::Trunc:
        fmt::println("trunc e{}", e.a);
        break;
      case T::SMax:
        fmt::println("smax(e{}, e{})", e.a, e.b);
        break;
      case T::UMax:
        fmt::println("umax(e{}, e{})", e.a, e.b);
        break;
      case T::Cmp:
        fmt::println("cmp {} e{}, e{}", e.pred, e.a, e.b);
        break;
      case T::Select:
        fmt::println("e{} ? e{} : e{}", e.a, e.b, e.c);
        break;
      case T::AddRec:
        fmt::println("{{e{},+,e{}}}", e.a, e.b);
        break;
    }
  }
  for (auto v : loop_vals) {
    auto it = lookup.find(v);
    if (it != lookup.end()) {
      fmt::println("  {} = e{}", v, it->second);
    }
  }
}

namespace {
// smallest k >= 0 with !pred(a + k * step, limit) for constants, nullopt if the
// compared value could wrap on the way or there is no such k
std::optional<i128> const_exit_iteration(ICmpSub pred, u32 bw, i128 a_raw,
                                         i128 step, i128 limit_raw) {
  const bool is_signed = pred == ICmpSub::SLT || pred == ICmpSub::SLE ||
                         pred == ICmpSub::SGT || pred == ICmpSub::SGE ||
                         pred == ICmpSub::NE || pred == ICmpSub::EQ;
  const i128 a = wrap_int(a_raw, bw, is_signed);
  const i128 limit = wrap_int(limit_raw, bw, is_signed);

  i128 k = 0;
  if (pred_holds(pred, a, limit)) {
    switch (pred) {
      case ICmpSub::SLT:
      case ICmpSub::ULT:
        if (step < 0) {
          return std::nullopt;
        }
        k = ceil_div(limit - a, step);
        break;
      case ICmpSub::SLE:
      case ICmpSub::ULE:
        if (step < 0) {
          return std::nullopt;
        }
        k = (limit - a) / step + 1;
        break;
      case ICmpSub::SGT:
      case ICmpSub::UGT:
        if (step > 0) {
          return std::nullopt;
        }
        k = ceil_div(a - limit, -step);
        break;
      case ICmpSub::SGE:
      case ICmpSub::UGE:
        if (step > 0) {
          return std::nullopt;
        }
        k = (a - limit) / (-step) + 1;
        break;
      case ICmpSub::NE: {
        const i128 dist = limit - a;
        if (dist % step != 0 || dist / step <= 0) {
          return std::nullopt;
        }
        k = dist / step;
        break;
      }
      case ICmpSub::EQ:
        k = 1;
        break;
      default:
        return std::nullopt;
    }
  }

  // the tested value must not wrap on the way (it is monotonic, so checking the
  // last iteration is enough)
  i128 delta = 0;
  if (__builtin_mul_overflow(k, step, &delta) ||
      !fits_int(a + delta, bw, is_signed)) {
    return std::nullopt;
  }
  return k;
}

// Same as above for a start / limit that are only loop invariant:
//   up   (step 1,  a + k < limit):  k = max(limit, a) - a
//   down (step -1, a - k > limit):  k = max(a, limit) - limit
// The tested value only moves towards the limit and stops on it, so it never
// wraps. The difference is non negative, for signed compares it additionally
// has to fit, which holds if the smaller side is non negative
SCEVRef symbolic_exit_iteration(ScalarEvo &evo, ICmpSub pred, SCEVRef a,
                                i128 step, SCEVRef limit) {
  bool up = false;
  bool is_signed = false;
  switch (pred) {
    case ICmpSub::SLT:
      up = true;
      is_signed = true;
      break;
    case ICmpSub::ULT:
      up = true;
      break;
    case ICmpSub::SGT:
      is_signed = true;
      break;
    case ICmpSub::UGT:
      break;
    default:
      return InvalidSCEV;
  }
  if (step != (up ? 1 : -1)) {
    return InvalidSCEV;
  }
  const SCEVRef lo = up ? a : limit;
  const SCEVRef other = up ? limit : a;
  if (is_signed && !evo.is_non_negative(lo)) {
    return InvalidSCEV;
  }
  const u32 bw = evo.expr(a).bw;
  const SCEVRef m = evo.mk_max(other, lo, is_signed);
  const SCEVRef r = evo.mk_add(m, evo.mk_mul(lo, evo.mk_const(-1, bw)));
  return evo.expr(r).t == SCEVExpr::Type::Unknown ? InvalidSCEV : r;
}
}  // namespace

namespace {
// Polynomials in the iteration number k with exact (non wrapping) coefficients,
// used for exit tests that are not affine (k * k < n).
constexpr u32 MaxPolyDeg = 3;
struct Poly {
  std::array<i128, MaxPolyDeg + 1> c{};
  u32 deg = 0;

  // all coefficients of k^1.. are >= 0 / <= 0
  [[nodiscard]] bool non_decreasing() const {
    return std::all_of(c.begin() + 1, c.end(), [](i128 x) { return x >= 0; });
  }
  [[nodiscard]] bool non_increasing() const {
    return std::all_of(c.begin() + 1, c.end(), [](i128 x) { return x <= 0; });
  }
  [[nodiscard]] std::optional<i128> eval(i128 k) const {
    i128 acc = 0;
    for (u32 i = deg + 1; i > 0; i--) {
      if (__builtin_mul_overflow(acc, k, &acc) ||
          __builtin_add_overflow(acc, c[i - 1], &acc)) {
        return std::nullopt;
      }
    }
    return acc;
  }
};

// A non constant node of the expression: its value must stay inside `bw` bits
// (signed, optionally also non negative) for the polynomial to be the value
// the program computes.
struct PolyNode {
  Poly p;
  u32 bw;
  bool non_neg;
};

std::optional<Poly> to_poly(const ScalarEvo &evo, SCEVRef r,
                            TVec<PolyNode> &nodes) {
  using T = SCEVExpr::Type;
  const SCEVExpr &e = evo.expr(r);
  if (e.bw == 0 || e.bw > 64) {
    return std::nullopt;
  }
  Poly res;
  switch (e.t) {
    case T::Const:
      res.c[0] = e.cval_i();
      return res;
    case T::AddRec: {
      if (!evo.is_const_addrec(r)) {
        return std::nullopt;
      }
      res.c[0] = evo.expr(e.a).cval_i();
      res.c[1] = evo.expr(e.b).cval_i();
      res.deg = 1;
      break;
    }
    case T::Add:
    case T::Mul: {
      auto x = to_poly(evo, e.a, nodes);
      auto y = to_poly(evo, e.b, nodes);
      if (!x || !y) {
        return std::nullopt;
      }
      if (e.t == T::Add) {
        res.deg = std::max(x->deg, y->deg);
        for (u32 i = 0; i <= res.deg; i++) {
          if (__builtin_add_overflow(x->c[i], y->c[i], &res.c[i])) {
            return std::nullopt;
          }
        }
      } else {
        if (x->deg + y->deg > MaxPolyDeg) {
          return std::nullopt;
        }
        res.deg = x->deg + y->deg;
        for (u32 i = 0; i <= x->deg; i++) {
          for (u32 j = 0; j <= y->deg; j++) {
            i128 t = 0;
            if (__builtin_mul_overflow(x->c[i], y->c[j], &t) ||
                __builtin_add_overflow(res.c[i + j], t, &res.c[i + j])) {
              return std::nullopt;
            }
          }
        }
      }
      break;
    }
    case T::SExt:
    case T::ZExt: {
      // the operand is the same number as long as it stays inside its width
      // (and is non negative for zext)
      const size_t before = nodes.size();
      auto x = to_poly(evo, e.a, nodes);
      if (!x) {
        return std::nullopt;
      }
      // a constant operand adds no node
      if (e.t == T::ZExt && nodes.size() > before) {
        nodes.back().non_neg = true;
      }
      return x;
    }
    default:
      return std::nullopt;
  }
  // every node has to be monotonic, so being inside the width at the first and
  // the last iteration means being inside it in between
  if (!res.non_decreasing() && !res.non_increasing()) {
    return std::nullopt;
  }
  nodes.push_back(PolyNode{.p = res, .bw = e.bw, .non_neg = false});
  return res;
}

// smallest k >= 0 with !pred(q(k), limit) for a q that is a polynomial of k
// with constant coefficients; nullopt if q could leave its bit width on the
// way or is not monotonic in the direction the compare needs. q is monotonic,
// so the iterations "still running and nothing wrapped" are a prefix and the
// exit is found by doubling and bisection.
std::optional<i128> poly_exit_iteration(const ScalarEvo &evo, ICmpSub pred,
                                        SCEVRef q, i128 limit_raw) {
  const bool is_signed = pred == ICmpSub::SLT || pred == ICmpSub::SLE ||
                         pred == ICmpSub::SGT || pred == ICmpSub::SGE;
  const bool up = pred == ICmpSub::SLT || pred == ICmpSub::SLE ||
                  pred == ICmpSub::ULT || pred == ICmpSub::ULE;
  const bool down = pred == ICmpSub::SGT || pred == ICmpSub::SGE ||
                    pred == ICmpSub::UGT || pred == ICmpSub::UGE;
  if (!up && !down) {
    return std::nullopt;
  }
  const u32 bw = evo.expr(q).bw;
  TVec<PolyNode> nodes;
  auto poly = to_poly(evo, q, nodes);
  if (!poly || nodes.empty()) {
    return std::nullopt;
  }
  // the compared value is the last node; unsigned compares need it non negative
  // (otherwise the reinterpretation is not monotonic)
  nodes.back().non_neg = nodes.back().non_neg || !is_signed;
  const i128 limit = wrap_int(limit_raw, bw, is_signed);

  auto fits = [&](i128 k) {
    return std::ranges::all_of(nodes, [&](const PolyNode &n) {
      auto v = n.p.eval(k);
      return v && fits_int(*v, n.bw, true) && (!n.non_neg || *v >= 0);
    });
  };
  auto holds = [&](i128 k) { return pred_holds(pred, *poly->eval(k), limit); };
  // still running at k without having wrapped
  auto running = [&](i128 k) { return fits(k) && holds(k); };

  if (!fits(0)) {
    return std::nullopt;
  }
  if (!holds(0)) {
    return 0;
  }
  if (up ? !poly->non_decreasing() : !poly->non_increasing()) {
    return std::nullopt;
  }
  // running(lo) holds, running(hi) does not
  i128 lo = 0;
  i128 hi = 1;
  while (running(hi)) {
    lo = hi;
    hi *= 2;
    if (hi > (static_cast<i128>(1) << 62)) {
      return std::nullopt;
    }
  }
  while (hi - lo > 1) {
    const i128 mid = lo + (hi - lo) / 2;
    (running(mid) ? lo : hi) = mid;
  }
  // left because the test failed, not because the value left its width
  if (!fits(hi)) {
    return std::nullopt;
  }
  return hi;
}
}  // namespace

bool TripCountAnalysis::update(CFG &cfg, Dominators &dom, LoopInfo &linfo,
                               ScalarEvo &evo) {
  cmp_rec = InvalidSCEV;
  cmp_signed = cmp_unsigned = false;
  if (linfo.leaving_nodes.size() != 1) {
    return false;
  }
  auto in_loop = [&](fir::BasicBlock bb) {
    return std::ranges::find(linfo.body_nodes, cfg.get_bb_id(bb)) !=
           linfo.body_nodes.end();
  };
  const u32 leaving = linfo.leaving_nodes[0];

  // the exit test has to be executed exactly once per header execution:
  // it dominates every back edge and there are no inner loops
  for (auto tail : linfo.tails) {
    if (!dom.dominates(leaving, tail)) {
      return false;
    }
  }
  for (auto node : linfo.body_nodes) {
    for (auto succ : cfg.bbrs[node].succ) {
      if (succ != linfo.head && dom.dominates(succ, node) &&
          std::ranges::find(linfo.body_nodes, succ) != linfo.body_nodes.end()) {
        return false;
      }
    }
  }

  auto term = cfg.bbrs[leaving].bb->get_terminator();
  if (!term->is(fir::InstrType::CondBranchInstr) || term->bbs.size() != 2) {
    return false;
  }
  const bool in0 = in_loop(term->bbs[0].bb);
  const bool in1 = in_loop(term->bbs[1].bb);
  if (in0 == in1) {
    return false;
  }
  auto condv = term->args[0];
  if (!condv.is_instr() || !condv.as_instr()->is(fir::InstrType::ICmp)) {
    return false;
  }
  auto cond = condv.as_instr();

  // predicate under which the loop keeps running: `q pred limit`
  auto pred = static_cast<ICmpSub>(cond->subtype);
  SCEVRef q = evo.get(cond->args[0]);
  SCEVRef limit_ref = evo.get(cond->args[1]);
  if ((!evo.is_addrec(q) && evo.is_addrec(limit_ref)) ||
      (evo.is_invariant(q) && !evo.is_invariant(limit_ref))) {
    std::swap(q, limit_ref);
    pred = swap_pred(pred);
  }
  if (!in0) {
    pred = negate_pred(pred);
  }
  if (pred == ICmpSub::INVALID || !evo.is_invariant(limit_ref) ||
      evo.is_invariant(q)) {
    return false;
  }
  if (!evo.is_addrec(q)) {
    // not affine: constant polynomials of the iteration number
    if (!evo.is_const(limit_ref)) {
      return false;
    }
    const auto k =
        poly_exit_iteration(evo, pred, q, evo.expr(limit_ref).cval_i());
    if (!k) {
      return false;
    }
    exit_iter = evo.mk_const(*k, evo.expr(q).bw);
    const_exit_iter = k;
    from_bb = cfg.bbrs[leaving].bb;
    to_bb = in0 ? term->bbs[1].bb : term->bbs[0].bb;
    return true;
  }
  cmp_rec = q;
  cmp_signed = pred == ICmpSub::SLT || pred == ICmpSub::SLE ||
               pred == ICmpSub::SGT || pred == ICmpSub::SGE;
  cmp_unsigned = pred == ICmpSub::ULT || pred == ICmpSub::ULE ||
                 pred == ICmpSub::UGT || pred == ICmpSub::UGE;
  const SCEVRef start_ref = evo.expr(q).a;
  const SCEVRef step_ref = evo.expr(q).b;
  const u32 bw = evo.expr(q).bw;
  if (bw == 0 || bw > 64 || evo.expr(limit_ref).bw != bw ||
      !evo.is_const(step_ref)) {
    return false;
  }
  const i128 step = evo.expr(step_ref).cval_i();
  if (step == 0) {
    return false;
  }

  std::optional<i128> k;
  if (evo.is_const(start_ref) && evo.is_const(limit_ref)) {
    k = const_exit_iteration(pred, bw, evo.expr(start_ref).cval_i(), step,
                             evo.expr(limit_ref).cval_i());
    if (!k) {
      return false;
    }
    exit_iter = evo.mk_const(*k, bw);
  } else {
    exit_iter = symbolic_exit_iteration(evo, pred, start_ref, step, limit_ref);
    if (exit_iter == InvalidSCEV) {
      return false;
    }
  }
  const_exit_iter = k;

  from_bb = cfg.bbrs[leaving].bb;
  to_bb = in0 ? term->bbs[1].bb : term->bbs[0].bb;
  return true;
}

void InductionEndValueAnalysis::update(CFG &cfg, Dominators &dom,
                                       LoopInfo &linfo, ScalarEvo &evo) {
  info.clear();

  TripCountAnalysis trip;
  if (!trip.update(cfg, dom, linfo, evo)) {
    return;
  }

  // value of everything we can express in the iteration the loop is left in
  TMap<fir::ValueR, SCEVRef> values;
  for (auto v : evo.loop_values()) {
    if (!v.get_type()->is_int()) {
      continue;
    }
    const SCEVRef cur = evo.get(v);
    const SCEVRef end = evo.at_iteration(cur, trip.exit_iter);
    if (end != InvalidSCEV && evo.is_expandable(end)) {
      values.insert({v, end});
    }
  }
  if (values.empty()) {
    return;
  }
  info.push_back(EndInfo{
      .from_bb = trip.from_bb,
      .to_bb = trip.to_bb,
      .values = std::move(values),
  });
}

void InductionEndValueAnalysis::dump(const ScalarEvo &evo) {
  fmt::println("Known Ends:");
  for (const auto &i : info) {
    fmt::println("@ {}=>{}:", i.from_bb, i.to_bb);
    for (const auto &[b, v] : i.values) {
      if (evo.is_const(v)) {
        fmt::println("  {}: {}", b, evo.expr(v).cval_i());
      } else {
        fmt::println("  {}: e{}", b, v);
      }
    }
  }
}

}  // namespace foptim::optim
