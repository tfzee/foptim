#pragma once
#include <optional>

#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/dominators.hpp"
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

  void dump() const;
};

class LoopInfoAnalysis {
private:
  template <class DomType> void update_impl(DomType &dom);

public:
  TVec<LoopInfo> info;

  LoopInfoAnalysis(Dominators &dom) { update(dom); }
  LoopInfoAnalysis(DominatorTree &dom) { update(dom); }

  void update(Dominators &dom);
  void update(DominatorTree &dom);
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
  // Const, Cmp: the predicate
  i128 cval = 0;
  // Invariant: the value, Input: the header argument. Otherwise the value
  // the expression was first built for (if any)
  fir::ValueR val;
  // Add/Mul/SMax/UMax/Cmp: operands, SExt/ZExt/Trunc: a, AddRec: a = start,
  // b = step, Select: a = condition, b = value if true, c = value if false
  SCEVRef a = InvalidSCEV;
  SCEVRef b = InvalidSCEV;
  SCEVRef c = InvalidSCEV;
};

// Scalar evolution of the values of one loop. The single source of truth for
// induction variables: every value in the loop gets an expression, header
// arguments that are `x += invariant` become add recurrences and anything
// built from them (x + 1, 4 * x + n, ...) is an add recurrence again.
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

  // set by update: the unique block outside of the loop that branches to the
  // header (invalid if there is none or more than one)
  fir::BasicBlock entry_bb{fir::BasicBlock::invalid()};

  SCEVRef push(const SCEVExpr &e);
  bool in_loop(fir::ValueR v) const;
  SCEVRef expr_of(fir::ValueR v, u32 depth);
  SCEVRef select_as_max(fir::Instr sel, u32 depth);
  // block argument of da block inside the loop (not the header): the common
  // expression of the values on all incoming edges
  SCEVRef block_arg_expr(fir::ValueR arg, u32 depth);
  // `e == h + step` with a loop invariant step
  SCEVRef extract_step(SCEVRef e, fir::ValueR h);

public:
  ScalarEvo(CFG &cfg, LoopInfo &info) { update(cfg, info); }
  void update(CFG &cfg, LoopInfo &info);

  SCEVRef mk_const(i128 v, u32 bw);
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
  SCEVRef mk_addrec(SCEVRef start, SCEVRef step);
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
// TODO: do other steps (ceil div by the step), <= / >= / != compares, which need
// overflow guards on the symbolic limit. one day
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

  TripCountAnalysis() = default;
  bool update(CFG &cfg, Dominators &dom, LoopInfo &info, ScalarEvo &evo);
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
  };

  TVec<EndInfo> info;
  InductionEndValueAnalysis(CFG &cfg, Dominators &dom, LoopInfo &info,
                            ScalarEvo &evo) {
    update(cfg, dom, info, evo);
  };
  void update(CFG &cfg, Dominators &dom, LoopInfo &info, ScalarEvo &evo);
  void dump(const ScalarEvo &evo);
};

} // namespace foptim::optim
