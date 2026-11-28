#pragma once
#include <algorithm>
#include <utility>

#include "mir/analysis/cfg.hpp"
#include "mir/analysis/live_variables.hpp"

namespace foptim::fmir {

// Which vregs hold the same value, from the copies between them. Two vregs
// that a mov made equal and that were not written since, on every path, can
// share a register even while both are alive. The relation is a forward
// dataflow of partitions (meet = vregs equal on every incoming edge), walked
// backwards inside a block with an undo log so the register collision pass can
// ask about any point. Vregs of 8 and 16 bit moves are never oncsidered equal
// sinc it keeps the upper bits of its destination.
class CopyEquality {
 public:
  // vregs with the same label are equal, labels are the smallest uid of the
  // class, only classes of two or more are listed
  using State = TVec<std::pair<u32, u32>>;

  CopyEquality(const MFunc &func, const CFG &cfg, size_t n_uids) : func_(func) {
    bool any = false;
    for (const auto &bb : func.bbs) {
      for (const auto &instr : bb.instrs) {
        any |= is_copy(instr);
      }
    }
    if (any) {
      cid_.assign(n_uids, 0);
      next_id_ = static_cast<u32>(n_uids) + 2;
      solve(cfg);
    }
  }

  // a and b are uids, true if they hold the same value at the current point
  [[nodiscard]] bool same(size_t a, size_t b) const {
    return valid_ && cid_[a] != 0 && cid_[a] == cid_[b];
  }

  // Positions the walk at the end of the block. step_back(i) then moves it
  // from after instruction i to before it.
  void begin_block(size_t bb) {
    if (!valid_) {
      return;
    }
    load(in_[bb]);
    run(bb, true);
  }
  void step_back(size_t instr_id) {
    if (!valid_) {
      return;
    }
    const size_t start = instr_id == 0 ? 0 : undo_end_[instr_id - 1];
    for (size_t j = undo_end_[instr_id]; j > start; j--) {
      cid_[undo_[j - 1].first] = undo_[j - 1].second;
    }
  }
  void end_block() {
    if (!valid_) {
      return;
    }
    reset();
  }

 private:
  static bool is_copy(const MInstr &instr) {
    if (!instr.is(GBaseSubtype::mov) || instr.n_args != 2 ||
        !instr.args[0].isReg() || !instr.args[1].isReg()) {
      return false;
    }
    const auto d = instr.args[0].reg;
    const auto s = instr.args[1].reg;
    return !d.is_concrete() && !s.is_concrete() && d != s &&
           d.is_vec_reg() == s.is_vec_reg() &&
           instr.args[0].ty == instr.args[1].ty &&
           instr.args[0].ty != Type::INVALID && get_size(instr.args[0].ty) >= 4;
  }

  void kill(size_t uid, bool record) {
    if (cid_[uid] != 0) {
      if (record) {
        undo_.emplace_back(static_cast<u32>(uid), cid_[uid]);
      }
      cid_[uid] = 0;
      changes_++;
    }
  }

  void join(size_t d, size_t s, bool record) {
    if (cid_[s] == 0) {
      if (record) {
        undo_.emplace_back(static_cast<u32>(s), 0);
      }
      cid_[s] = next_id_++;
      touched_.push_back(static_cast<u32>(s));
    }
    if (record) {
      undo_.emplace_back(static_cast<u32>(d), cid_[d]);
    }
    cid_[d] = cid_[s];
    touched_.push_back(static_cast<u32>(d));
    changes_++;
  }

  // the transfer function of the block, from the state in cid
  void run(size_t bb, bool record) {
    undo_.clear();
    undo_end_.clear();
    bool seen_branch = false;
    for (const auto &instr : func_.bbs[bb].instrs) {
      const size_t before = changes_;
      helper_.clear();
      written_args(instr, helper_);
      for (const auto &w : helper_) {
        if (w.arg.isReg() && !w.arg.reg.is_concrete()) {
          kill(reg_to_uid(w.arg.reg), record);
        }
      }
      if (is_copy(instr)) {
        join(reg_to_uid(instr.args[0].reg), reg_to_uid(instr.args[1].reg),
             record);
      }
      // the state of the block end is used for every edge
      if (seen_branch && changes_ != before) {
        sound_ = false;
      }
      if (instr.has_bb_ref || instr.is(GJumpSubtype::jmp_indirect)) {
        seen_branch = true;
      }
      if (record) {
        undo_end_.push_back(undo_.size());
      }
    }
  }

  void load(const State &s) {
    for (const auto &[uid, label] : s) {
      cid_[uid] = label + 1;
      touched_.push_back(uid);
    }
  }

  void reset() {
    for (auto uid : touched_) {
      cid_[uid] = 0;
    }
    touched_.clear();
  }

  // the state in cid as labels, clears cid
  State extract() {
    std::ranges::sort(touched_);
    touched_.erase(std::ranges::unique(touched_).begin(), touched_.end());
    TMap<u32, std::pair<u32, u32>> classes;
    for (auto uid : touched_) {
      if (cid_[uid] == 0) {
        continue;
      }
      auto [it, inserted] = classes.try_emplace(cid_[uid], uid, 0U);
      (void)inserted;
      it->second.second++;
    }
    State res;
    for (auto uid : touched_) {
      if (cid_[uid] == 0) {
        continue;
      }
      const auto &c = classes.at(cid_[uid]);
      if (c.second >= 2) {
        res.emplace_back(uid, c.first);
      }
    }
    reset();
    return res;
  }

  // the pairs equal in both states
  static State meet(const State &a, const State &b) {
    struct Group {
      u32 label;
      u32 n;
    };
    TMap<u64, Group> groups;
    TVec<std::pair<u32, u64>> members;
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() && j < b.size()) {
      if (a[i].first < b[j].first) {
        i++;
      } else if (a[i].first > b[j].first) {
        j++;
      } else {
        const u64 key = (static_cast<u64>(a[i].second) << 32) | b[j].second;
        auto [it, inserted] =
            groups.try_emplace(key, Group{.label = a[i].first, .n = 0});
        (void)inserted;
        it->second.n++;
        members.emplace_back(a[i].first, key);
        i++;
        j++;
      }
    }
    State res;
    for (const auto &[uid, key] : members) {
      const auto &g = groups.at(key);
      if (g.n >= 2) {
        res.emplace_back(uid, g.label);
      }
    }
    return res;
  }

  void solve(const CFG &cfg) {
    const size_t n = func_.bbs.size();
    // reverse post order from the entry block
    TVec<u32> post;
    TVec<char> seen(n, 0);
    TVec<std::pair<u32, size_t>> stack;
    stack.emplace_back(0, 0);
    seen[0] = 1;
    while (!stack.empty()) {
      auto &top = stack.back();
      const auto &succ = cfg.bbrs[top.first].succ;
      if (top.second < succ.size()) {
        const u32 s = succ[top.second++];
        if (seen[s] == 0) {
          seen[s] = 1;
          stack.emplace_back(s, 0);
        }
      } else {
        post.push_back(top.first);
        stack.pop_back();
      }
    }
    std::ranges::reverse(post);

    in_.assign(n, State{});
    out_.assign(n, State{});
    TVec<char> have_out(n, 0);
    // meets that are too coarse are refined until nothing changes, a limit
    // guards against a function that does not converge
    for (size_t iter = 0; iter < 32; iter++) {
      bool changed = false;
      for (u32 bb : post) {
        State entry;
        bool first = true;
        if (bb != 0) {
          for (u32 p : cfg.bbrs[bb].pred) {
            if (have_out[p] == 0) {
              continue;
            }
            entry = first ? out_[p] : meet(entry, out_[p]);
            first = false;
          }
        }
        in_[bb] = std::move(entry);
        load(in_[bb]);
        run(bb, false);
        State o = extract();
        if (have_out[bb] == 0 || o != out_[bb]) {
          out_[bb] = std::move(o);
          have_out[bb] = 1;
          changed = true;
        }
      }
      if (!changed) {
        valid_ = sound_;
        return;
      }
    }
    valid_ = false;
  }

  const MFunc &func_;
  bool valid_ = false;
  bool sound_ = true;
  // class of every uid, 0 for a vreg that is not known to be equal to another
  TVec<u32> cid_;
  u32 next_id_ = 0;
  size_t changes_ = 0;
  TVec<u32> touched_;
  TVec<std::pair<u32, u32>> undo_;
  TVec<size_t> undo_end_;
  TVec<ArgData> helper_;
  TVec<State> in_;
  TVec<State> out_;
};

}  // namespace foptim::fmir
