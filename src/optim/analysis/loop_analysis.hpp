#pragma once
#include "ir/basic_block_arg.hpp"
#include "ir/function.hpp"
#include "optim/analysis/dominators.hpp"

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
public:
  TVec<LoopInfo> info;

  LoopInfoAnalysis(Dominators &dom) { update(dom); }
  void update(Dominators &dom);
  void dump() const;
};

class LoopRangeAnalysis {
public:
  fir::BBArgument induction_var;
  fir::Use lower_bound_var;
  fir::Use upper_bound_var;
  bool known_lower = false;
  i128 lower_bound = 0;
  bool known_upper = false;
  i128 upper_bound = 0;

  enum IterationType {
    PlusA,
  };
  IterationType type = IterationType::PlusA;
  i128 a = 0;
  LoopRangeAnalysis()
      : induction_var(fir::BBArgument::invalid()),
        lower_bound_var(fir::Use::norm(fir::Instr{fir::Instr::invalid()}, 0)),
        upper_bound_var(fir::Use::norm(fir::Instr{fir::Instr::invalid()}, 0)) {}

  bool update(CFG &cfg, LoopInfo &info);
  void dump() const;
};

} // namespace foptim::optim
