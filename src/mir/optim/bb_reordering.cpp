#include "bb_reordering.hpp"

#include "mir/analysis/cfg.hpp"
#include "mir/instr.hpp"

namespace foptim::fmir {

namespace {
// Every block ends in explicit control flow (cjmp* followed by a jmp, ret or
// unreach) so any permutation is correct so de layout only decides which jumps
// become fallthroughs and backend just clenass those up
struct LayoutInfo {
  TVec<u32> loop_depth;
  TVec<u8> cold;
};

LayoutInfo analyze(const MFunc &func, const CFG &cfg) {
  const u32 n = func.bbs.size();
  LayoutInfo info;
  info.loop_depth.assign(n, 0);
  info.cold.assign(n, 0);

  enum : u8 { White = 0, Grey = 1, Black = 2 };
  TVec<u8> color(n, White);
  TVec<TVec<u32>> tails(n);
  TVec<std::pair<u32, u32>> stack; // (block, next succ index)
  color[0] = Grey;
  stack.emplace_back(0, 0);
  while (!stack.empty()) {
    auto &[bb, next] = stack.back();
    if (next < cfg.bbrs[bb].succ.size()) {
      const u32 s = cfg.bbrs[bb].succ[next++];
      if (color[s] == White) {
        color[s] = Grey;
        stack.emplace_back(s, 0);
      } else if (color[s] == Grey) {
        tails[s].push_back(bb);
      }
    } else {
      color[bb] = Black;
      stack.pop_back();
    }
  }

  TVec<u32> mark(n, 0);
  TVec<u32> work;
  for (u32 h = 0; h < n; h++) {
    if (tails[h].empty()) {
      continue;
    }
    mark[h] = h + 1;
    work.clear();
    for (u32 t : tails[h]) {
      if (mark[t] != h + 1) {
        mark[t] = h + 1;
        work.push_back(t);
      }
    }
    while (!work.empty()) {
      const u32 b = work.back();
      work.pop_back();
      for (u32 p : cfg.bbrs[b].pred) {
        // only blocks reachable from the entry belong to the loop
        if (mark[p] != h + 1 && color[p] == Black) {
          mark[p] = h + 1;
          work.push_back(p);
        }
      }
    }
    for (u32 b = 0; b < n; b++) {
      if (mark[b] == h + 1) {
        info.loop_depth[b]++;
      }
    }
  }

  // cold: unreachable either unreach or all succs are cold
  work.clear();
  for (u32 b = 0; b < n; b++) {
    const auto &instrs = func.bbs[b].instrs;
    if (color[b] != Black ||
        (!instrs.empty() && instrs.back().is(GBaseSubtype::unreach))) {
      info.cold[b] = 1;
      work.push_back(b);
    }
  }
  while (!work.empty()) {
    const u32 b = work.back();
    work.pop_back();
    for (u32 p : cfg.bbrs[b].pred) {
      if (info.cold[p]) {
        continue;
      }
      bool all_cold = true;
      for (u32 s : cfg.bbrs[p].succ) {
        all_cold &= info.cold[s] != 0;
      }
      if (all_cold) {
        info.cold[p] = 1;
        work.push_back(p);
      }
    }
  }
  return info;
}
} // namespace

void BBReordering::apply(MFunc &func, const conf::CompConf &) {
  const u32 n = func.bbs.size();
  if (n <= 2) {
    return;
  }
  const CFG cfg(func);
  const LayoutInfo info = analyze(func, cfg);

  TVec<u32> order;
  order.reserve(n);
  TVec<u8> placed(n, 0);

  auto jmp_target = [&](u32 bb) -> u32 {
    const auto &instrs = func.bbs[bb].instrs;
    if (!instrs.empty() && instrs.back().is(GJumpSubtype::jmp)) {
      return instrs.back().bb_ref;
    }
    return n;
  };

  // Best unplaced successor never leave a loop while the loop continues,
  // never pull cold code into hot code and prefer  the jmp default target since that
  // one becomes a free fallthrough
  auto pick_next = [&](u32 bb) -> u32 {
    const u32 jt = jmp_target(bb);
    u32 best = n;
    auto key = [&](u32 s) {
      const bool exits_loop = info.loop_depth[s] < info.loop_depth[bb];
      return std::tuple{!exits_loop, s == jt, ~s};
    };
    for (u32 s : cfg.bbrs[bb].succ) {
      if (placed[s] || (info.cold[s] && !info.cold[bb])) {
        continue;
      }
      if (best == n || key(s) > key(best)) {
        best = s;
      }
    }
    return best;
  };

  auto build_chain = [&](u32 start) {
    for (u32 bb = start; bb != n; bb = pick_next(bb)) {
      placed[bb] = 1;
      order.push_back(bb);
    }
  };

  // entry first then remaining hot chains then cold chains
  build_chain(0);
  for (u8 cold_pass = 0; cold_pass < 2; cold_pass++) {
    for (u32 b = 0; b < n; b++) {
      if (!placed[b] && info.cold[b] == cold_pass) {
        build_chain(b);
      }
    }
  }
  ASSERT(order.size() == n);

  TVec<u32> new_id(n);
  bool changed = false;
  for (u32 i = 0; i < n; i++) {
    new_id[order[i]] = i;
    changed |= order[i] != i;
  }
  if (!changed) {
    return;
  }

  IRVec<MBB> new_bbs;
  new_bbs.reserve(n);
  for (u32 old_id : order) {
    new_bbs.push_back(MBB{std::move(func.bbs[old_id].instrs)});
  }
  func.bbs = std::move(new_bbs);
  for (auto &bb : func.bbs) {
    for (auto &instr : bb.instrs) {
      if (instr.has_bb_ref) {
        instr.bb_ref = new_id[instr.bb_ref];
      }
    }
  }
}

} // namespace foptim::fmir
