#pragma once
#include <fmt/base.h>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/function_pass.hpp"
namespace foptim::optim {

// move conditional statments that are independent of the loop outside the loop
//  creating two subloops without a condition. This enables further
//  optimizations
class LoopUnswitch final : public FunctionPass {
 public:
  struct HelperData {
    BitSet<> a;
    BitSet<> condIf;
    BitSet<> condElse;
    BitSet<> c;
    fir::ContextData::V2VMap map;

    void reset(CFG &cfg) {
      if (cfg.bbrs.size() != a.bit_size()) {
        a = BitSet<>::empty(cfg.bbrs.size());
        condIf = BitSet<>::empty(cfg.bbrs.size());
        condElse = BitSet<>::empty(cfg.bbrs.size());
        c = BitSet<>::empty(cfg.bbrs.size());
      } else {
        a.reset(false);
        condIf.reset(false);
        condElse.reset(false);
        c.reset(false);
      }
      map.clear();
    }
  };

  bool apply(fir::Context &ctx, CFG &cfg, LoopInfo &info, HelperData &help);
  void apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
