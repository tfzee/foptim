#pragma once
#include "ir/function.hpp"
#include "optim/analysis/dominators.hpp"

namespace foptim::optim {

struct LoopInfo {
  //the first node into the loop (kinda dominating the whole loop)
  u32 head;
  //all backwards edges into header
  TVec<u32> tails;
  //all nodes including header and tails
  TVec<u32> body_nodes;
  //all nodes that leave
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
