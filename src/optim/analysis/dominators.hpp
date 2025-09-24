#pragma once
#include <deque>

#include "ir/basic_block_ref.hpp"
#include "optim/analysis/cfg.hpp"
#include "utils/bitset.hpp"

namespace foptim::optim {
using utils::BitSet;

class Dominators {
 public:
  struct Node {
    fir::BasicBlock bb;
    i64 im_dom;

    BitSet<> dominators;
    // BitSet postdominators;
    BitSet<> frontier;
  };

  TVec<Node> dom_bbs;
  const CFG *cfg;

  Dominators(const CFG &cfg) : cfg(&cfg) { update(cfg); }

  void dump() const {
    fmt::println("DUMP DOM");

    for (const auto &node : dom_bbs) {
      fmt::println("BB: {:p}", (void *)node.bb.get_raw_ptr());
      fmt::println(" Dominators {}", node.dominators);
      fmt::println(" Frontier {}", node.frontier);
      // print << "BB: " << node.bb.get_raw_ptr() << "\n  Dominators:";
      // print << node.dominators << "\n";
      // print << "  PostDom: " << node.postdominators << "\n";
      // print << "  Frontier: " << node.frontier << "\n";
    }
  }

  void update(const CFG &cfg) {
    ZoneScopedNC("DOM UPDATE", COLOR_ANALY);
    ASSERT(cfg.bbrs.size() != 0 &&
           "Cant have empty cfg prob running on a invalid function or a "
           "declaration");
    dom_bbs.clear();
    this->cfg = &cfg;

    const auto &cfg_bbs = cfg.bbrs;
    const size_t n_bbs = cfg.bbrs.size();

    dom_bbs.reserve(n_bbs);

    BitSet fullBitSet{n_bbs, true};
    BitSet emptyBitSet{n_bbs, false};

    for (const auto &bbr : cfg.bbrs) {
      dom_bbs.push_back(Node{.bb = bbr.bb,
                             .im_dom = -1,
                             .dominators = fullBitSet,
                             .frontier = emptyBitSet});
    }

    std::deque<u32, utils::TempAlloc<u32>> worklist{cfg.entry};

    BitSet newSet = {n_bbs, false};
    while (!worklist.empty()) {
      u32 cur = worklist.front();
      worklist.pop_front();

      const auto &pred = cfg_bbs[cur].pred;
      if (pred.empty()) {
        newSet.reset(false);
      } else {
        newSet.assign(dom_bbs[pred[0]].dominators);
        for (size_t i = 1; i < pred.size(); i++) {
          auto &dom = dom_bbs[pred[i]];
          newSet *= dom.dominators;
        }
      }

      newSet[cur].set(true);
      if (newSet != dom_bbs[cur].dominators) {
        dom_bbs[cur].dominators.assign(newSet);
        for (auto succ : cfg_bbs[cur].succ) {
          worklist.push_back(succ);
        }
      }
    }

    BitSet doms{n_bbs, false};
    // BitSet strict_dom{n_bbs, false};

    // frontier
    // iter over all blocks look at each successor
    for (u32 node_id = 0; node_id < dom_bbs.size(); node_id++) {
      for (u32 succ_id : cfg_bbs[node_id].succ) {
        // if a succ has less dominators then the parent -> its a frontier
        doms.assign(dom_bbs[succ_id].dominators);
        doms[succ_id].set(false);
        doms.xor_(dom_bbs[node_id].dominators).mul(dom_bbs[node_id].dominators);
        for (auto dom : doms) {
          dom_bbs[dom].frontier[succ_id].set(true);
        }
      }
    }
  }
};

}  // namespace foptim::optim
