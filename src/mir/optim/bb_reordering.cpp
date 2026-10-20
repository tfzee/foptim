#include "bb_reordering.hpp"

#include "mir/analysis/block_info.hpp"
#include "mir/analysis/cfg.hpp"
#include "mir/instr.hpp"

namespace foptim::fmir {

// Every block ends in explicit control flow (cjmp* followed by a jmp, ret or
// unreach) so any permutation is correct so de layout only decides which jumps
// become fallthroughs and backend just clenass those up

namespace {
// cjmp T followed by jmp F where T is the next block becomes cjmp !cond F, the
// jmp to the next block is dropped by the backend
void invert_branches_to_next(MFunc &func) {
  auto inverse = [](GJumpSubtype t, GJumpSubtype &out) {
    switch (t) {
      case GJumpSubtype::cjmp_int_slt:
        out = GJumpSubtype::cjmp_int_sge;
        return true;
      case GJumpSubtype::cjmp_int_sge:
        out = GJumpSubtype::cjmp_int_slt;
        return true;
      case GJumpSubtype::cjmp_int_sle:
        out = GJumpSubtype::cjmp_int_sgt;
        return true;
      case GJumpSubtype::cjmp_int_sgt:
        out = GJumpSubtype::cjmp_int_sle;
        return true;
      case GJumpSubtype::cjmp_int_ult:
        out = GJumpSubtype::cjmp_int_uge;
        return true;
      case GJumpSubtype::cjmp_int_uge:
        out = GJumpSubtype::cjmp_int_ult;
        return true;
      case GJumpSubtype::cjmp_int_ule:
        out = GJumpSubtype::cjmp_int_ugt;
        return true;
      case GJumpSubtype::cjmp_int_ugt:
        out = GJumpSubtype::cjmp_int_ule;
        return true;
      case GJumpSubtype::cjmp_int_eq:
        out = GJumpSubtype::cjmp_int_ne;
        return true;
      case GJumpSubtype::cjmp_int_ne:
        out = GJumpSubtype::cjmp_int_eq;
        return true;
      default:
        return false;
    }
  };
  for (u32 b = 0; b + 1 < func.bbs.size(); b++) {
    auto &instrs = func.bbs[b].instrs;
    if (instrs.size() < 2 || !instrs.back().is(GJumpSubtype::jmp)) {
      continue;
    }
    auto &cj = instrs[instrs.size() - 2];
    auto &jm = instrs.back();
    GJumpSubtype inv;
    if (cj.bop != GOpcode::GJmp || !cj.has_bb_ref || cj.bb_ref != b + 1 ||
        jm.bb_ref == b + 1 ||
        !inverse(static_cast<GJumpSubtype>(cj.sop), inv)) {
      continue;
    }
    cj.sop = static_cast<u32>(inv);
    std::swap(cj.bb_ref, jm.bb_ref);
  }
}
}  // namespace

void BBReordering::apply(MFunc &func, const conf::CompConf &) {
  const u32 n = func.bbs.size();
  if (n <= 2) {
    return;
  }
  const CFG cfg(func);
  const BlockInfo info = analyze_blocks(func, cfg);

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
  // never pull cold code into hot code and prefer  the jmp default target since
  // that one becomes a free fallthrough
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
    invert_branches_to_next(func);
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
  invert_branches_to_next(func);
}

}  // namespace foptim::fmir
