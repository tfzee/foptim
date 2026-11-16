#pragma once
#include <algorithm>

#include "mir/func.hpp"

namespace foptim::fmir {

class CFG {
 public:
  struct Node {
    TVec<u32> pred;
    TVec<u32> succ;
  };
  enum class IterRes {
    None = 0,
    Changed = 1,
  };

  TVec<Node> bbrs;
  bool is_reversed;

  CFG() = default;
  CFG(const MFunc &func, bool reverse = false) : is_reversed(reverse) {
    update(func, reverse);
  }

  void update(const MFunc &func, bool reverse) {
    ZoneScopedN("CFG UPDATE");
    bbrs.clear();

    bbrs.reserve(func.bbs.size());

    for (size_t i = 0; i < func.bbs.size(); i++) {
      bbrs.push_back(Node{.pred = {}, .succ = {}});
    }

    const auto &bbs = func.bbs;

    for (size_t from = 0; from < bbs.size(); from++) {
      for (const auto &instr : bbs[from].instrs) {
        if (instr.has_bb_ref) {
          bbrs[from].succ.push_back(instr.bb_ref);
          bbrs[instr.bb_ref].pred.push_back(from);
        } else if (instr.is(GJumpSubtype::jmp_indirect)) {
          // a target that appears several times is one edge
          const auto &targets = func.jump_tables[instr.bb_ref].targets;
          TVec<u32> seen(targets.begin(), targets.end());
          std::ranges::sort(seen);
          seen.erase(std::ranges::unique(seen).begin(), seen.end());
          for (auto target : seen) {
            bbrs[from].succ.push_back(target);
            bbrs[target].pred.push_back(from);
          }
        }
      }
    }

    if (reverse) {
      // TODO: should (and how would) i change entry ?
      for (auto &node : bbrs) {
        std::swap(node.pred, node.succ);
      }
    }
  }
};

}  // namespace foptim::fmir
