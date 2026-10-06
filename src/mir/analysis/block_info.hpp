#pragma once
#include <cmath>

#include "mir/analysis/cfg.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

// Cheap static block frequency information: how deep a block is nested in
// loops (back edges found by a DFS from the entry, natural loop bodies by
// walking the predecessors of the tails) and whether it is cold (unreachable
// or only leads to `unreach`).
struct BlockInfo {
  TVec<u32> loop_depth;
  TVec<u8> cold;
};

inline BlockInfo analyze_blocks(const MFunc &func, const CFG &cfg) {
  const u32 n = func.bbs.size();
  BlockInfo info;
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

// Relative execution frequency of a block, an iteration of a loop is assumed to
// run 8 times and cold blocks hardly at all
inline f32 block_weight(u32 loop_depth, bool cold) {
  if (cold) {
    return 0.1F;
  }
  return std::pow(8.F, static_cast<f32>(std::min<u32>(loop_depth, 5)));
}

} // namespace foptim::fmir
