#pragma once
#include "ir/basic_block_ref.hpp"
#include "optim/analysis/cfg.hpp"
#include "utils/arena.hpp"
#include "utils/bitset.hpp"
#include "utils/logging.hpp"

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
    ZoneScopedN("DOM UPDATE");

    dom_bbs.clear();
    this->cfg = &cfg;

    const auto &cfg_bbs = cfg.bbrs;
    const size_t n_bbs = cfg.bbrs.size();

    dom_bbs.reserve(n_bbs);

    BitSet fullBitSet{n_bbs, true};
    BitSet emptyBitSet{n_bbs, false};

    for (size_t i = 0; i < cfg.bbrs.size(); i++) {
      dom_bbs.push_back(Node{cfg.bbrs[i].bb, -1, fullBitSet, emptyBitSet});
    }

    std::deque<u32, utils::TempAlloc<u32>> worklist{cfg.entry};

    BitSet newSet = {n_bbs, false};
    while (!worklist.empty()) {
      u32 cur = worklist.front();
      worklist.pop_front();

      const auto &pred = cfg_bbs[cur].pred;
      newSet.reset(false);
      if (!pred.empty()) {
        newSet.assign(dom_bbs[pred[0]].dominators);
        for (size_t i = 1; i < pred.size(); i++) {
          auto &dom = dom_bbs[pred[i]];
          // if (dom.dominators[cur]) {
          //   continue;
          // }
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
    BitSet strict_dom{n_bbs, false};

    // frontier
    // iter over all blocks look at each successor
    for (u32 node_id = 0; node_id < dom_bbs.size(); node_id++) {

      for (u32 succ_id : cfg_bbs[node_id].succ) {
        // if a succ has less dominators then the parent -> its a frontier
        strict_dom.assign(dom_bbs[succ_id].dominators);
        strict_dom[succ_id].set(false);
        doms.assign(dom_bbs[node_id].dominators)
            .xor_(strict_dom)
            .mul(dom_bbs[node_id].dominators);
        // doms.assign().assign(strict_dom).negate();

        // auto doms =
        //     (dom_bbs[node_id].dominators ^ dom_bbs[succ_id].dominators) *
        //     dom_bbs[node_id].dominators;
        for (auto dom : doms) {
          dom_bbs[dom].frontier[succ_id].set(true);
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
