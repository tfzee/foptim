#pragma once
#include <algorithm>

#include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::optim {

class CFG {
 public:
  struct Node {
    fir::BasicBlock bb;
    TVec<u32> pred;
    TVec<u32> succ;
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
  void dump_graph(const char *filename = "out.dot") const;

  void update(fir::Function &func, bool reverse) {
    ZoneScopedNC("CFG UPDATE", COLOR_ANALY);
    entry = 0;

    bbrs.clear();
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

  /* updates bb_id so its succ points to its old succ + others succ aswell as
  updating the blocks from other.succ so they have the bb as a pred */
  void update_merge_succ(u32 bb_id, u32 other) {
    for (auto s : bbrs[other].succ) {
      if (std::ranges::contains(bbrs[bb_id].succ, s)) {
        continue;
      }
      bbrs[bb_id].succ.push_back(s);
      bbrs[s].pred.push_back(bb_id);
    }
  }
  /* Removes all succ from the bb and all the pred from the bbs it previously
  jumped too acting like the terminator got removed from this bb */
  void update_delete_term(u32 bb_id) {
    for (auto s : bbrs[bb_id].succ) {
      for (size_t predip = bbrs[s].pred.size(); predip > 0; predip--) {
        if (bbrs[s].pred[predip - 1] == bb_id) {
          bbrs[s].pred.erase(bbrs[s].pred.begin() + predip - 1);
        }
      }
    }
  }

  void update_delete(u32 bb_id) {
    for (auto &bb : bbrs) {
      for (size_t predip = bb.pred.size(); predip > 0; predip--) {
        if (bb.pred[predip - 1] == bb_id) {
          bb.pred.erase(bb.pred.begin() + predip - 1);
        } else if (bb.pred[predip - 1] > bb_id) {
          bb.pred[predip - 1]--;
        }
      }
      for (size_t succip = bb.succ.size(); succip > 0; succip--) {
        if (bb.succ[succip - 1] == bb_id) {
          bb.succ.erase(bb.succ.begin() + succip - 1);
        } else if (bb.succ[succip - 1] > bb_id) {
          bb.succ[succip - 1]--;
        }
      }
    }
    bbrs.erase(bbrs.begin() + bb_id);
  }
};

}  // namespace foptim::optim
