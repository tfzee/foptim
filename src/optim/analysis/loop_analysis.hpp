#pragma once
#include <optional>

#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/dominators.hpp"
#include "utils/APInt.hpp"
#include "utils/bitset.hpp"
#include "utils/map.hpp"
#include "utils/set.hpp"

namespace foptim::optim {

struct LoopInfo {
  // the first node into the loop (kinda dominating the whole loop)
  u32 head;
  // all backwards edges into header
  TVec<u32> tails;
  // all nodes including header and tails
  TVec<u32> body_nodes;
  // all nodes that leave
  TVec<u32> leaving_nodes;
  // bb_id -> in body_nodes, avoids scanning body_nodes
  utils::BitSet<> body_set;

  [[nodiscard]] bool contains(u32 bb_id) const { return body_set[bb_id]; }

  void dump() const;
};

class LoopInfoAnalysis {
 private:
  template <class DomType>
  void update_impl(DomType &dom);

 public:
  TVec<LoopInfo> info;

  LoopInfoAnalysis(Dominators &dom) { update(dom); }
  LoopInfoAnalysis(DominatorTree &dom) { update(dom); }

  void update(Dominators &dom);
  void update(DominatorTree &dom);
  void dump() const;
};

// nesting over LoopInfoAnalysis::info, index i is loop i. the parent is the
// smallest other loop containing it, loops with different headers are disjoint
// or nested so it is a forest. built on demand, a snapshot of the loops
class LoopNesting {
 public:
  // per loop, index of the enclosing loop, -1 for an outermost one
  TVec<i32> parent;
  TVec<TVec<u32>> children;
  // body nodes of the loop that are not in a nested loop
  TVec<TVec<u32>> own_blocks;
  // the loops without a parent
  TVec<u32> roots;
  // bb id -> the innermost loop containing it, -1 outside of every loop
  TVec<i32> innermost_of;

  LoopNesting() = default;
  LoopNesting(const CFG &cfg, const TVec<LoopInfo> &loops) {
    update(cfg, loops);
  }
  void update(const CFG &cfg, const TVec<LoopInfo> &loops);

  [[nodiscard]] size_t size() const { return parent.size(); }
  // inner is nested in outer, not the same loop
  [[nodiscard]] bool is_nested_in(u32 inner, u32 outer) const;
  void dump() const;
};

class LoopRangeAnalysis {
 public:
  enum IterationType {
    PlusA,
  };
  i128 lower_bound = 0;
  i128 upper_bound = 0;
  i128 a = 0;
  fir::BBArgument induction_var;
  fir::Use lower_bound_var;
  fir::Use upper_bound_var;
  IterationType type = IterationType::PlusA;
  bool known_lower = false;
  bool known_upper = false;

  LoopRangeAnalysis()
      : induction_var(fir::BBArgument::invalid()),
        lower_bound_var(fir::Use::norm(fir::Instr{fir::Instr::invalid()}, 0)),
        upper_bound_var(fir::Use::norm(fir::Instr{fir::Instr::invalid()}, 0)) {}

  bool update(CFG &cfg, LoopInfo &info);
  void dump() const;
  void reset();
};

using SCEVRef = u32;
inline constexpr SCEVRef InvalidSCEV = ~static_cast<SCEVRef>(0);

struct SCEVExpr {
  enum class Type : u8 {
    // dont know (loop variant)
    Unknown,
    // compile time constant
    Const,
    Invariant,
    // header argument, only exists while the recurrences are classified
    Input,

    Add,
    Mul,
    SExt,
    ZExt,
    Trunc,
    SMax,
    UMax,
    // a >> pred (logical, a invariant)
    LShr,
    Cmp,
    // idk if this is super useful
    Select,
    // {start,+,step}: start + k * step in iteration k (k = 0 first execution
    // of the header). start and step are loop invariant
    AddRec,
  };
  Type t = Type::Unknown;
  // value is the same in every iteration
  bool invariant = false;
  u32 bw = 0;
  // Const: the value, bw bits
  APInt cval;
  // Cmp: the predicate
  u32 pred = 0;
  // Invariant: the value, Input: the header argument. Otherwise the value
  // the expression was first built for (if any)
  fir::ValueR val;
  // Add/Mul/SMax/UMax/Cmp: operands, SExt/ZExt/Trunc: a, AddRec: a = start,
  // b = step, Select: a = condition, b = value if true, c = value if false
  SCEVRef a = InvalidSCEV;
  SCEVRef b = InvalidSCEV;
  SCEVRef c = InvalidSCEV;

  // the constant as a signed number (truncated above 128 bits)
  [[nodiscard]] i128 cval_i() const { return cval.to_i128(); }
};

// Scalar evolution of the values of one loop. The single source of truth for
// induction variables: every value in the loop gets an expression, header
// arguments that are `x += invariant` become add recurrences and anything
// built from them (x + 1, 4 * x + n, ...) is an add recurrence again. A header
// argument whose back edge value is a recurrence {s + c,+,c} and whose start
// is s is the recurrence {s,+,c} too (what LoopRotate makes of a header value
// like base + 8 * i that the body uses).
class ScalarEvo {
  TVec<SCEVExpr> exprs;
  // structurally equal nodes share one SCEVRef (hash -> candidates), Unknown
  // and Input nodes are never shared
  TMap<u64, TVec<SCEVRef>> interned;
  TMap<fir::ValueR, SCEVRef> lookup;
  TSet<u32> body;
  // values defined inside the loop that have an expression
  TVec<fir::ValueR> loop_vals;
  CFG *cfg = nullptr;
  LoopInfo *loop = nullptr;

  TVec<SCEVRef> rec_start;
  TVec<SCEVRef> rec_step;
  // per header argument the invariant start (InvalidSCEV if not known) and the
  // values of the back edges
  TVec<SCEVRef> arg_start;
  TVec<TVec<fir::ValueR>> arg_backs;
  bool derive_recurrences();
  // narrow recurrences known not to wrap in the executed iterations
  struct NoWrap {
    SCEVRef rec;
    bool is_signed;
  };
  TVec<NoWrap> no_wrap;
  // when specualte set, ext of a recurrence that is not known not to wrap is
  // the wide recurrence anyway and the recurrence is recorded in assumed, this
  // requires a check at run time that none of them wraps in the iterations it
  // runs. Not yet used
  bool speculate = false;
  TVec<NoWrap> assumed;
  void classify_values();

  // set by update: the unique block outside of the loop that branches to the
  // header (invalid if there is none or more than one)
  fir::BasicBlock entry_bb{fir::BasicBlock::invalid()};

  SCEVRef push(const SCEVExpr &e);
  [[nodiscard]] bool in_loop(fir::ValueR v) const;
  SCEVRef expr_of(fir::ValueR v, u32 depth);
  SCEVRef decode_arith(fir::Instr instr, u32 bw, u32 depth);
  SCEVRef expand_invariants(SCEVRef r, u32 depth);
  bool same_sum(SCEVRef a, SCEVRef b);
  SCEVRef select_as_max(fir::Instr sel, u32 depth);
  // block argument of da block inside the loop (not the header): the common
  // expression of the values on all incoming edges
  SCEVRef block_arg_expr(fir::ValueR v, u32 depth);
  // `e == h + step` with a loop invariant step
  SCEVRef extract_step(SCEVRef e, fir::ValueR h);

 public:
  ScalarEvo(CFG &cfg, LoopInfo &info, bool speculative = false)
      : speculate(speculative) {
    update(cfg, info);
  }
  // the recurrences ext assumed not to wrap, only filled when speculative
  // (the ones assume_no_wrap proved afterwards are left out)
  [[nodiscard]] TVec<NoWrap> assumed_no_wrap() const {
    TVec<NoWrap> r;
    for (const auto &a : assumed) {
      bool proven = false;
      for (const auto &nw : no_wrap) {
        proven |= nw.is_signed == a.is_signed && same_expr(nw.rec, a.rec);
      }
      if (!proven) {
        r.push_back(a);
      }
    }
    return r;
  }
  void update(CFG &cfg, LoopInfo &info);

  SCEVRef mk_const(i128 v, u32 bw);
  SCEVRef mk_const(APInt v);
  SCEVRef mk_invariant(fir::ValueR v);
  SCEVRef mk_unknown(u32 bw);
  SCEVRef mk_add(SCEVRef a, SCEVRef b);
  SCEVRef mk_mul(SCEVRef a, SCEVRef b);
  SCEVRef mk_max(SCEVRef a, SCEVRef b, bool is_signed);
  SCEVRef mk_cmp(fir::ICmpInstrSubType pred, SCEVRef a, SCEVRef b);
  SCEVRef mk_select(SCEVRef cond, SCEVRef t, SCEVRef f);
  // widening / narrowing to bw bits, `a` itself if the width is the same
  SCEVRef mk_resize(SCEVRef a, u32 bw, bool is_signed);
  SCEVRef mk_ext(SCEVRef a, bool is_signed, u32 bw);
  SCEVRef mk_trunc(SCEVRef a, u32 bw);
  // logical shift right by a constant amount, a has to be loop invariant
  SCEVRef mk_lshr(SCEVRef a, u32 amount);
  SCEVRef mk_addrec(SCEVRef start, SCEVRef step);
  // q (constant step) does not wrap in the executed iterations, so ext of q
  // and of q - step become recurrences of the wide type
  // last_iter is the number of the last iteration the loop runs when it is
  // known, q plus a few steps (a[i + 2]) cannot wrap either while the counter
  // stays that far from the end of the type
  // limit is the bound of a signed q < limit exit test with step one, when
  // it is known to fit a narrower type (sext of a narrow value, small
  // constant) so does q, and trunc of q is a recurrence that sext brings back
  void assume_no_wrap(SCEVRef q, bool is_signed,
                      std::optional<i128> last_iter = std::nullopt,
                      SCEVRef limit = InvalidSCEV);
  // the narrowest of 8, 16 and 32 bits such that a value of limit is not
  // negative and fits it as a signed number, 0 if unknown
  u32 signed_fit_bits(SCEVRef limit) const;
  bool same_expr(SCEVRef a, SCEVRef b) const;

  // The expression of r in iteration k (a loop invariant expression of the
  // iteration count, k is interpreted as unsigned). InvalidSCEV if some part of
  // r is not understood. at_iteration of a recurrence {s,+,c} is s + k * c.
  SCEVRef at_iteration(SCEVRef r, SCEVRef k);

  // Can the expression be turned back into instructions (SCEVExpander): a tree
  // of constants, integer invariants and the operations above.
  [[nodiscard]] bool is_expandable(SCEVRef r) const;
  // r >= 0 when interpreted as signed (conservative)
  [[nodiscard]] bool is_non_negative(SCEVRef r) const;

  // the block outside of the loop whose terminator branches to the header, all
  // invariant values dominate its end (invalid if there is no unique one)
  [[nodiscard]] fir::BasicBlock entry_block() const { return entry_bb; }

  [[nodiscard]] const SCEVExpr &expr(SCEVRef r) const { return exprs[r]; }
  // expression of any value (outside values are Invariant/Const)
  SCEVRef get(fir::ValueR v);

  [[nodiscard]] bool is_const(SCEVRef r) const {
    return exprs[r].t == SCEVExpr::Type::Const;
  }
  [[nodiscard]] bool is_addrec(SCEVRef r) const {
    return exprs[r].t == SCEVExpr::Type::AddRec;
  }
  // add recurrence with constant start and step
  [[nodiscard]] bool is_const_addrec(SCEVRef r) const {
    return is_addrec(r) && is_const(exprs[r].a) && is_const(exprs[r].b);
  }
  [[nodiscard]] bool is_invariant(SCEVRef r) const {
    return exprs[r].invariant;
  }
  // value in iteration k, if every leaf is an constant (bw <= 64)
  [[nodiscard]] std::optional<i128> eval_at(SCEVRef r, i128 k) const;
  // values defined inside the loop that have an expression
  [[nodiscard]] const TVec<fir::ValueR> &loop_values() const {
    return loop_vals;
  }
  void dump() const;
};

// Closed form trip count of loops whose only exit is a compare of an affine
// expression (add recurrence with constant step) against da loop invariant
// limit. The exit test has to run exactly once per header execution. The
// analysis fails if the compared value could wrap.
// With constant start and limit every step is supported, otherwise (symbolic)
// only steps of +-1 with <, > compares (signed ones need the smaller of start
// and limit to be known non negative so the difference cannot overflow).
// <= and >= are the same with the limit moved by one, for a step that does not
// wrap or a bound that is the counter of an enclosing loop below the maximum.
// TODO: do other symbolic steps (ceil div by da step)
class TripCountAnalysis {
 public:
  // the edge leaving the loop
  fir::BasicBlock from_bb{fir::BasicBlock::invalid()};
  fir::BasicBlock to_bb{fir::BasicBlock::invalid()};
  // header executions before the exit test fires as a loop invariant
  // expression (bit width of the exit compare, modular): a recurrence {s,+,c}
  // has the value s + exit_iter * c when the loop is left. The loop body runs
  // exit_iter + 1 times.
  SCEVRef exit_iter = InvalidSCEV;
  // exit_iter if it is a compile time constant (not wrapped to the bit width)
  std::optional<i128> const_exit_iter;
  // the compared recurrence and the signedness of its predicate
  SCEVRef cmp_rec = InvalidSCEV;
  // the bound of cmp_rec for a strict signed exit test, else InvalidSCEV
  SCEVRef cmp_limit = InvalidSCEV;
  bool cmp_signed = false;
  bool cmp_unsigned = false;

  TripCountAnalysis() = default;
  // loops around other loops are only accepted if the exit test is not part of
  // an inner loop and allow_inner_loops is set
  // outer_loops lets a `<=` exit be solved when the bound is the counter of an
  // enclosing loop that provably stays below the type maximum
  bool update(CFG &cfg, Dominators &dom, LoopInfo &info, ScalarEvo &evo,
              bool allow_inner_loops = false,
              const TVec<LoopInfo> *outer_loops = nullptr);
};

// Values of the loop's values when the loop is left through the trip count
// exit, as invariant expressions (constants if everything is known).
class InductionEndValueAnalysis {
 public:
  struct EndInfo {
    fir::BasicBlock from_bb;
    fir::BasicBlock to_bb;
    // every expression can be expanded (ScalarEvo::is_expandable)
    TMap<fir::ValueR, SCEVRef> values;
    // pointer values of the loop: the pointer in front of the loop plus an
    // expandable byte offset
    struct PtrEnd {
      fir::ValueR base;
      SCEVRef offset;
    };
    TMap<fir::ValueR, PtrEnd> ptr_values;
  };

  TVec<EndInfo> info;
  InductionEndValueAnalysis(CFG &cfg, Dominators &dom, LoopInfo &info,
                            ScalarEvo &evo) {
    update(cfg, dom, info, evo);
  };
  void update(CFG &cfg, Dominators &dom, LoopInfo &info, ScalarEvo &evo);
  void dump(const ScalarEvo &evo);
};

}  // namespace foptim::optim
