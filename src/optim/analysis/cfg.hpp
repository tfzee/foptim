#pragma once
#include <deque>

#include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "utils/arena.hpp"
#include "utils/bitset.hpp"

namespace foptim::optim {

class CFG {
 public:
  struct Node {
    fir::BasicBlock bb;
    TVec<u32> pred;
    TVec<u32> succ;
  };
  enum class IterRes {
    None = 0,
    Changed = 1,
  };

  TVec<Node> bbrs;
  u32 entry;
  fir::Function *func = nullptr;
  bool is_reversed;
  // FVec<u32> exits;

  CFG() = default;
  CFG(fir::Function &func, bool reverse = false)
      : func(&func), is_reversed(reverse) {
    update(func, reverse);
  }

  [[nodiscard]] u32 get_bb_id(fir::BasicBlock search_bb) const {
    for (u32 id = 0; id < bbrs.size(); id++) {
      if (bbrs[id].bb == search_bb) {
        return id;
      }
    }
    ASSERT(false);
    std::abort();
  }

  void dump() const;
  void dump_graph() const;

  void update(fir::Function &func, bool reverse) {
    ZoneScopedNC("CFG UPDATE", COLOR_ANALY);
    bbrs.clear();
    entry = 0;

    bbrs.reserve(func.n_bbs());

    for (auto &bb : func.get_bbs()) {
      if (bb == func.get_entry()) {
        entry = bbrs.size();
      }
      bbrs.push_back(Node{.bb = bb, .pred = {}, .succ = {}});
    }

    const auto &bbs = func.get_bbs();

    for (size_t from = 0; from < bbs.size(); from++) {
      const auto terminator = bbs[from]->instructions.back();
      if (terminator->is(fir::InstrType::ReturnInstr)) {
        continue;
      }
      bbrs[from].succ.reserve(terminator->is(fir::InstrType::BranchInstr) ? 1
                                                                          : 2);
      for (const auto &target : terminator->get_bb_args()) {
        for (u32 j = 0; j < bbrs.size(); j++) {
          if (bbrs[j].bb == target.bb) {
            bbrs[from].succ.push_back(j);
            bbrs[j].pred.push_back(from);
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

  template <class T>
  constexpr void postorder(T &&functor) {
    std::deque<u32, utils::TempAlloc<u32>> queue{entry};
    utils::BitSet set{bbrs.size(), false};
    set[entry].set(true);

    while (!queue.empty()) {
      auto next = queue.front();
      for (auto child : bbrs[next].succ) {
        if (!set[child]) {
          queue.push_back(child);
          set[child].set(true);
        }
      }
      // skip if one got invalidated
      if (next < bbrs.size() && bbrs[next].bb.is_valid()) {
        auto res = functor(bbrs[next]);
        // TODO: handle this
        if (res == CFG::IterRes::Changed) {
          update(*func, is_reversed);
          set.reset(false);
          queue.clear();
          queue.push_back(entry);
          continue;
        }
      }
      queue.pop_front();
    }
  }
};

}  // namespace foptim::optim
