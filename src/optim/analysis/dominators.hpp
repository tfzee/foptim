#pragma once
#include "ir/basic_block_ref.hpp"
#include "optim/analysis/cfg.hpp"
#include "utils/bitset.hpp"
#include "utils/logging.hpp"

namespace foptim::optim {
using utils::BitSet;

class Dominators {
public:
  struct Node {
    fir::BasicBlock bb;
    i64 im_dom;

    BitSet dominators;
    // BitSet postdominators;
    BitSet frontier;
  };

  FVec<Node> dom_bbs;
  const CFG *cfg;

  Dominators(const CFG &cfg) : cfg(&cfg) { update(cfg); }

  void dump() const {
    utils::Debug << "DUMP DOM\n";

    for (const auto &node : dom_bbs) {
      utils::Debug << "BB: " << node.bb.get_raw_ptr() << "\n  Dominators:";
      utils::Debug << node.dominators << "\n";
      // utils::Debug << "  PostDom: " << node.postdominators << "\n";
      utils::Debug << "  Frontier: " << node.frontier << "\n";
    }
  }

  void update(const CFG &cfg) {
    ZoneScopedN("DOM UPDATE");

    dom_bbs.clear();
    this->cfg = &cfg;

    
    const FVec<CFG::Node> &cfg_bbs = cfg.bbrs;
    const size_t n_bbs = cfg.bbrs.size();

    dom_bbs.reserve(n_bbs);

    BitSet fullBitSet{n_bbs, true};
    BitSet emptyBitSet{n_bbs, false};
    // utils::Debug << "TODO POST DOMINATORS\n";

    for (const auto & bbr : cfg.bbrs) {
      // if (i == 0) {
      dom_bbs.push_back(
          Node{bbr.bb, -1, fullBitSet, emptyBitSet});
      // } else {
      //   dom_bbs.push_back(
      //       Node{cfg.bbrs[i].bb, -1, fullBitSet, emptyBitSet, emptyBitSet});
      // }
    }

    std::deque<u32> worklist{cfg.entry};

    while (!worklist.empty()) {
      u32 cur = worklist.front();
      worklist.pop_front();

      const auto &pred = cfg_bbs[cur].pred;
      BitSet newSet = {n_bbs, false};
      if (!pred.empty()) {
        newSet = dom_bbs[pred[0]].dominators;
        for (size_t i = 1; i < pred.size(); i++) {
          auto &dom = dom_bbs[pred[i]];
          newSet *= dom.dominators;
        }
      }

      newSet[cur] = true;

      if (newSet != dom_bbs[cur].dominators) {
        dom_bbs[cur].dominators = newSet;
        for (auto succ : cfg_bbs[cur].succ) {
          worklist.push_back(succ);
        }
      }
    }

    BitSet doms{n_bbs, false};

    // frontier
    // iter over all blocks look at each successor
    for (u32 node_id = 0; node_id < dom_bbs.size(); node_id++) {
      for (u32 succ_id : cfg_bbs[node_id].succ) {
        // if a succ has less dominators then the parent -> its a frontier
        doms.assign(dom_bbs[node_id].dominators)
            .xor_(dom_bbs[succ_id].dominators)
            .mul(dom_bbs[node_id].dominators);

        // auto doms =
        //     (dom_bbs[node_id].dominators ^ dom_bbs[succ_id].dominators) *
        //     dom_bbs[node_id].dominators;
        for (auto dom : doms) {
          dom_bbs[dom].frontier[succ_id] = true;
        }

        // for (size_t parent_elem : dom_bbs[node_id].dominators) {
        //   bool succ_also_has_it = false;
        //   for (size_t succ_elem : dom_bbs[succ_id].dominators) {
        //     if (succ_elem == parent_elem) {
        //       succ_also_has_it = true;
        //       break;
        //     }
        //   }
        //   if (!succ_also_has_it) {
        //     // so this is a frontier for this parent_element
        //     dom_bbs[parent_elem].frontier[succ_id] = true;
        //   }
        // }
      }
    }
  }
};

} // namespace foptim::optim
