#pragma once
#include "ir/function.hpp"
#include "optim/analysis/dominators.hpp"

namespace foptim::optim {

struct LoopInfo {
  u32 head;
  FVec<u32> tails;
  FVec<u32> body_nodes;
  FVec<u32> leaving_nodes;

  void dump() const;
};

class LoopInfoAnalysis {
public:
  FVec<LoopInfo> info;

  LoopInfoAnalysis(Dominators &dom) { update(dom); }
  void update(Dominators &dom);
  void dump() const;
};

} // namespace foptim::optim
