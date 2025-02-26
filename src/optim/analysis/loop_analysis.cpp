#include "loop_analysis.hpp"
#include "utils/arena.hpp"
#include "utils/bitset.hpp"
#include "utils/logging.hpp"
#include "utils/vec.hpp"
#include <algorithm>

namespace foptim::optim {

void LoopInfoAnalysis::update(Dominators &dom) {
  info.clear();

  const CFG &cfg = *dom.cfg;
  TVec<u32> deq{};

  // if a bb has a incoming edge from a block that it dominates we found a loop
  // header
  for (u32 bb_id = 0; bb_id < cfg.bbrs.size(); bb_id++) {
    TVec<u32> tails;
    for (auto pred : cfg.bbrs[bb_id].pred) {
      if (dom.dom_bbs[pred].dominators[bb_id]) {
        tails.push_back(pred);
      }
    }
    // we found an loop header + tails
    if (tails.size() > 0) {

      // TODO move alloc out of loop
      utils::BitSet forward{cfg.bbrs.size(), false};
      utils::BitSet backward{cfg.bbrs.size(), false};

      forward[bb_id].set(true);

      deq.clear();
      deq.reserve(32);
      deq.push_back(bb_id);

      while (!deq.empty()) {
        u32 curr = deq.back();
        deq.pop_back();
        for (auto succ : cfg.bbrs[curr].succ) {
          if (!forward[succ]) {
            forward[succ].set(true);
            deq.push_back(succ);
          }
        }
      }

      TVec<u32> body_nodes;
      body_nodes.reserve(1 + 1 + tails.size() * 2);

      // if one of the potential body nodes is not dominated by the header
      //  we do not consider this a real loop even though there are
      //  backwards edges
      bool invalid_loop = false;

      for (auto tail : tails) {
        backward[tail].set(true);
        if (!dom.dom_bbs[tail].dominators[bb_id]) {
          invalid_loop = true;
          break;
        }
        body_nodes.push_back(tail);
        deq.push_back(tail);
      }
      if (invalid_loop) {
        break;
      }

      while (!deq.empty()) {
        u32 curr = deq.back();
        deq.pop_back();
        for (auto pred : cfg.bbrs[curr].pred) {
          if (forward[pred] && !backward[pred]) {
            backward[pred].set(true);
            if (!dom.dom_bbs[pred].dominators[bb_id]) {
              invalid_loop = true;
              break;
            }
            body_nodes.push_back(pred);
            deq.push_back(pred);
          }
        }
        if (invalid_loop) {
          break;
        }
      }
      if (invalid_loop) {
        break;
      }

      TVec<u32> leaving_nodes;
      leaving_nodes.reserve(body_nodes.size() / 2);
      for (u32 node : body_nodes) {
        for (auto succ : cfg.bbrs[node].succ) {
          if (std::find(body_nodes.begin(), body_nodes.end(), succ) ==
              body_nodes.end()) {
            leaving_nodes.push_back(node);
          }
        }
      }

      info.push_back({
          bb_id,
          std::move(tails),
          std::move(body_nodes),
          std::move(leaving_nodes),
      });
    }
  }
}

void LoopInfo::dump() const {
  fmt::println("Loop\n  Header: {}\n  Body: {}\n  Tails: {}\n  Leaving: {}\n", head, body_nodes, tails, leaving_nodes);
}
void LoopInfoAnalysis::dump() const {
  for (const auto &loop : info) {
    loop.dump();
  }
}
} // namespace foptim::optim
