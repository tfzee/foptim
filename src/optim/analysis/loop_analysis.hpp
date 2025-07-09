#pragma once
#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value_ref.hpp"
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

class InductionVarAnalysis {
public:
  enum IterationType {
    PlusConst,
    SubConst,
    MulConst,
    Other,
  };
  struct InductionVar {
    fir::ValueR def;
    fir::ConstantValueR consti;
    IterationType type = IterationType::Other;
  };
  struct IInductionVar {
    fir::ValueR def;
    fir::ValueR arg1;
    fir::ValueR arg2;
    IterationType type = IterationType::Other;
  };

  std::optional<InductionVar>
  _check_if_direct_induct(fir::BBArgument v, u32 arg_id,
                          TVec<std::pair<fir::Instr, u32>> backwards_jumps,
                          CFG &cfg, LoopInfo &info);
  // do not depend on other induction vars
  TVec<InductionVar> direct_inductvars;

  // depend on other induction vars
  TVec<IInductionVar> indirect_inductvars;

  InductionVarAnalysis(CFG &cfg, LoopInfo &info) { update(cfg, info); }
  void update(CFG &cfg, LoopInfo &info);
  void dump() const;
};

class InductionEndValueAnalysis {
public:
  struct EndInfo {
    fir::BasicBlock from_bb;
    fir::BasicBlock to_bb;
    TMap<fir::ValueR, i128> values;
  };

  TVec<EndInfo> info;
  InductionEndValueAnalysis(CFG &cfg, LoopInfo &info,
                            InductionVarAnalysis &ianal) {
    update(cfg, info, ianal);
  };
  void update(CFG &cfg, LoopInfo &info, InductionVarAnalysis &ianal);
  void dump();
};

} // namespace foptim::optim
