#pragma once
#include "ir/function.hpp"
#include "optim/analysis/dominators.hpp"

namespace foptim::optim {

struct LoopInfo {
  u32 head;
  TVec<u32> tails;
  TVec<u32> body_nodes;
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

} // namespace foptim::optim
