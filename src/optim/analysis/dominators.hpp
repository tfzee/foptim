#pragma once
#include <deque>
#include <fmt/base.h>
#include <optional>

#include "ir/basic_block_ref.hpp"
#include "optim/analysis/cfg.hpp"
#include "utils/bitset.hpp"

namespace foptim::optim {
using utils::BitSet;

class Dominators {
public:
  struct Node {
    fir::BasicBlock bb;
    BitSet<> dominators;
    // BitSet postdominators;
    BitSet<> frontier;
  };

  TVec<Node> dom_bbs;
  const CFG *cfg;

  Dominators() : cfg(nullptr) {}
  Dominators(const CFG &cfg) : cfg(&cfg) { update(cfg); }

  // a dominates b
  [[nodiscard]] bool strict_dominates(fir::BasicBlock a,
                                      fir::BasicBlock b) const {
    return dom_bbs[cfg->get_bb_id(b)].dominators[cfg->get_bb_id(a)];
  }

  // bb1 dominates bb2
  [[nodiscard]] bool strict_dominates(u32 bb1, u32 bb2) const {
    return dom_bbs[bb2].dominators[bb1];
  }

  [[nodiscard]] bool dominates(u32 bb1, u32 bb2) const {
    return bb2 == bb1 || dom_bbs[bb2].dominators[bb1];
  }

  [[nodiscard]] const BitSet<> &get_frontier(fir::BasicBlock a) const {
    return dom_bbs[cfg->get_bb_id(a)].frontier;
  }

  [[nodiscard]] const BitSet<> &strict_dominators(fir::BasicBlock a) const {
    return dom_bbs[cfg->get_bb_id(a)].dominators;
  }

  [[nodiscard]] const BitSet<> &strict_dominators(u32 bb_id) const {
    return dom_bbs[bb_id].dominators;
  }

  [[nodiscard]] u32 common_denom(u32 bb1_id, u32 bb2_id) const {
    // NOTE: just switch to using a dominator tree :)
    if (bb1_id == bb2_id || bb1_id == 0) {
      return bb1_id;
    }
    if (bb2_id == 0) {
      return bb2_id;
    }
    if (strict_dominates(bb2_id, bb1_id)) {
      return bb2_id;
    }
    if (strict_dominates(bb1_id, bb2_id)) {
      return bb1_id;
    }
    TVec<u32> que1;
    TVec<u32> que2;
    TVec<u32> helper;
    que1.reserve(dom_bbs.size() / 2);
    que2.reserve(dom_bbs.size() / 2);
    helper.reserve(dom_bbs.size() / 2);

    for (auto dom : cfg->bbrs[bb1_id].pred) {
      que1.push_back(dom);
    }
    for (auto dom : cfg->bbrs[bb2_id].pred) {
      que2.push_back(dom);
    }

    const auto check_overlap = [](const TVec<u32> &a1,
                                  const TVec<u32> &a2) -> std::optional<u32> {
      // TODO: since they ordered we could optimize this
      for (auto a : a1) {
        for (auto b : a2) {
          if (a == b) {
            return a;
          }
        }
      }
      return std::nullopt;
    };
    const auto step = [&helper, this](TVec<u32> &inp) {
      helper.clear();
      for (auto q : inp) {
        for (auto dom : cfg->bbrs[q].pred) {
          helper.push_back(dom);
        }
      }
      inp.clear();
      inp.insert(inp.begin(), helper.begin(), helper.end());
    };

    while (true) {
      if (auto v = check_overlap(que1, que2)) {
        return v.value();
      }
      helper.clear();
      step(que1);
      step(que2);
    }
  }

  void dump() const {
    fmt::println("DUMP DOM");

    for (const auto &node : dom_bbs) {
      fmt::println("BB: {:p}",
                   reinterpret_cast<const void *>(node.bb.get_raw_ptr()));
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
      dom_bbs.push_back(Node{
          .bb = bbr.bb, .dominators = fullBitSet, .frontier = emptyBitSet});
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
    dom_bbs[cfg.entry].dominators = BitSet(n_bbs, false);

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

class DominatorTree {
public:
  struct Node {
    fir::BasicBlock bb;
    u32 idom = ~0U;  // immediate dominator (bb id); entry's idom == its own id
    u32 depth = 0;   // depth in the dominator tree, entry == 0
    u32 dfs_in = 0;  // preorder index of this node's Euler-tour entry
    u32 dfs_out = 0; // one-past the largest dfs_in in this node's subtree
  };

  TVec<Node> dom_bbs;
  const CFG *cfg = nullptr;

  DominatorTree() = default;
  DominatorTree(const CFG &cfg) { update(cfg); }

  [[nodiscard]] u32 idom(u32 bb_id) const { return dom_bbs[bb_id].idom; }
  [[nodiscard]] u32 idom(fir::BasicBlock bb) const {
    return dom_bbs[cfg->get_bb_id(bb)].idom;
  }

  // does a strictly dominate b
  [[nodiscard]] bool strict_dominates(fir::BasicBlock a,
                                      fir::BasicBlock b) const {
    return strict_dominates(cfg->get_bb_id(a), cfg->get_bb_id(b));
  }

  [[nodiscard]] bool strict_dominates(u32 bb1, u32 bb2) const {
    return bb1 != bb2 && dominates(bb1, bb2);
  }

  // does bb1 dominate bb2 (non-strict: a block dominates itself)
  [[nodiscard]] bool dominates(u32 bb1, u32 bb2) const {
    const auto &a = dom_bbs[bb1];
    const auto &b = dom_bbs[bb2];
    return a.dfs_in <= b.dfs_in && b.dfs_in < a.dfs_out;
  }

  [[nodiscard]] bool dominates(fir::BasicBlock a, fir::BasicBlock b) const {
    return dominates(cfg->get_bb_id(a), cfg->get_bb_id(b));
  }

  // nearest common dominator of bb1/bb2 == LCA on the dominator tree.
  [[nodiscard]] u32 common_denom(u32 bb1_id, u32 bb2_id) const {
    while (bb1_id != bb2_id) {
      while (dom_bbs[bb1_id].depth > dom_bbs[bb2_id].depth) {
        bb1_id = dom_bbs[bb1_id].idom;
      }
      while (dom_bbs[bb2_id].depth > dom_bbs[bb1_id].depth) {
        bb2_id = dom_bbs[bb2_id].idom;
      }
      if (bb1_id != bb2_id) {
        bb1_id = dom_bbs[bb1_id].idom;
        bb2_id = dom_bbs[bb2_id].idom;
      }
    }
    return bb1_id;
  }

  void dump() const {
    fmt::println("DUMP DOM");
    for (u32 i = 0; i < dom_bbs.size(); i++) {
      const auto &node = dom_bbs[i];
      fmt::println("BB {}: {:p} idom={} depth={}", i,
                   reinterpret_cast<const void *>(node.bb.get_raw_ptr()),
                   node.idom, node.depth);
    }
  }

  void update(const CFG &cfg) {
    ZoneScopedNC("DOM UPDATE", COLOR_ANALY);
    ASSERT(cfg.bbrs.size() != 0 &&
           "Cant have empty cfg prob running on a invalid function or a "
           "declaration");
    this->cfg = &cfg;
    const auto &cfg_bbs = cfg.bbrs;
    const u32 n_bbs = static_cast<u32>(cfg_bbs.size());

    dom_bbs.clear();
    dom_bbs.reserve(n_bbs);
    for (const auto &bbr : cfg_bbs) {
      dom_bbs.push_back(Node{.bb = bbr.bb});
    }

    // reverse-postorder numbering via an iterative DFS over successors
    TVec<u32, utils::TempAlloc<u32>> postorder(n_bbs, ~0U);
    TVec<u32, utils::TempAlloc<u32>> rpo;
    rpo.reserve(n_bbs);
    {
      TVec<u8, utils::TempAlloc<u8>> visited(n_bbs, 0);
      TVec<std::pair<u32, u32>, utils::TempAlloc<std::pair<u32, u32>>> stack;
      stack.reserve(n_bbs);
      stack.emplace_back(cfg.entry, 0);
      visited[cfg.entry] = 1;

      TVec<u32, utils::TempAlloc<u32>> post_order_ids;
      post_order_ids.reserve(n_bbs);

      while (!stack.empty()) {
        auto &[node, next_idx] = stack.back();
        const auto &succs = cfg_bbs[node].succ;
        if (next_idx < succs.size()) {
          u32 s = succs[next_idx++];
          if (visited[s] == 0U) {
            visited[s] = 1;
            stack.emplace_back(s, 0);
          }
        } else {
          post_order_ids.push_back(node);
          stack.pop_back();
        }
      }
      for (u32 i = 0; i < post_order_ids.size(); i++) {
        postorder[post_order_ids[i]] = i;
      }
      rpo.assign(post_order_ids.rbegin(), post_order_ids.rend());
    }

    // Cooper/Harvey/Kennedy iterative idom computation, O(E) per pass
    for (auto &n : dom_bbs) {
      n.idom = ~0U;
    }
    dom_bbs[cfg.entry].idom = cfg.entry;

    const auto intersect = [&](u32 b1, u32 b2) {
      while (b1 != b2) {
        while (postorder[b1] < postorder[b2]) {
          b1 = dom_bbs[b1].idom;
        }
        while (postorder[b2] < postorder[b1]) {
          b2 = dom_bbs[b2].idom;
        }
      }
      return b1;
    };

    bool changed = true;
    while (changed) {
      changed = false;
      for (u32 bb : rpo) {
        if (bb == cfg.entry) {
          continue;
        }
        const auto &pred = cfg_bbs[bb].pred;
        u32 new_idom = ~0U;
        for (u32 p : pred) {
          if (dom_bbs[p].idom == ~0U) {
            continue; // predecessor not processed yet
          }
          new_idom = (new_idom == ~0U) ? p : intersect(p, new_idom);
        }
        if (dom_bbs[bb].idom != new_idom) {
          dom_bbs[bb].idom = new_idom;
          changed = true;
        }
      }
    }

    //handle unreachable bbs gracefully
    for (auto &n : dom_bbs) {
      if (n.idom == ~0U) {
        n.idom = cfg.entry;
      }
    }

    // build the dominator tree as a flat CSR array (no per-node vectors)
    TVec<u32, utils::TempAlloc<u32>> child_count(n_bbs, 0);
    for (u32 i = 0; i < n_bbs; i++) {
      if (i != cfg.entry) {
        child_count[dom_bbs[i].idom]++;
      }
    }
    TVec<u32, utils::TempAlloc<u32>> child_offset(n_bbs + 1, 0);
    for (u32 i = 0; i < n_bbs; i++) {
      child_offset[i + 1] = child_offset[i] + child_count[i];
    }
    TVec<u32, utils::TempAlloc<u32>> children(child_offset[n_bbs]);
    {
      TVec<u32, utils::TempAlloc<u32>> cursor(child_offset.begin(),
                                              child_offset.end() - 1);
      for (u32 i = 0; i < n_bbs; i++) {
        if (i != cfg.entry) {
          children[cursor[dom_bbs[i].idom]++] = i;
        }
      }
    }

    // iterative preorder walk of the dominator tree: depth + Euler-tour in/out
    dom_bbs[cfg.entry].depth = 0;
    {
      TVec<std::pair<u32, u32>, utils::TempAlloc<std::pair<u32, u32>>> stack;
      stack.reserve(n_bbs);
      u32 timer = 0;
      dom_bbs[cfg.entry].dfs_in = timer++;
      stack.emplace_back(cfg.entry, child_offset[cfg.entry]);

      while (!stack.empty()) {
        auto &[node, cursor] = stack.back();
        u32 end = child_offset[node + 1];
        if (cursor < end) {
          u32 c = children[cursor++];
          dom_bbs[c].depth = dom_bbs[node].depth + 1;
          dom_bbs[c].dfs_in = timer++;
          stack.emplace_back(c, child_offset[c]);
        } else {
          dom_bbs[node].dfs_out = timer;
          stack.pop_back();
        }
      }
    }
  }
};

} // namespace foptim::optim
