#include "optim/analysis/mem/memory_ssa.hpp"

#include <algorithm>

#include "ir/basic_block.hpp"
#include "ir/instruction_data.hpp"
#include "utils/logging.hpp"

namespace foptim::optim {

namespace {
bool is_def(MemAccess::Kind k) {
  return k == MemAccess::Kind::Store || k == MemAccess::Kind::BarrierRW;
}
}  // namespace

MemorySSA::MemorySSA(const CFG &cfg, const Dominators &dom,
                     const MemAccessInfo &info) {
  const u32 n_bbs = static_cast<u32>(cfg.bbrs.size());
  acc.emplace_back();
  block_phi.assign(n_bbs, None);

  // accesses per block in program order
  TVec<TVec<u32>> per_block(n_bbs);
  TVec<u8> has_def(n_bbs, 0);
  const auto &all = info.accesses();
  for (u32 i = 0; i < all.size(); i++) {
    const u32 b = cfg.get_bb_id(all[i].instr->get_parent());
    per_block[b].push_back(i);
    if (is_def(all[i].kind) && dom.is_reachable(b)) {
      has_def[b] = 1;
    }
  }

  // phis at the iterated dominance frontier of the blocks with a def
  {
    TVec<u32> work;
    for (u32 b = 0; b < n_bbs; b++) {
      if (has_def[b] != 0) {
        work.push_back(b);
      }
    }
    while (!work.empty()) {
      const u32 b = work.back();
      work.pop_back();
      for (const u32 f : dom.get_frontier(b)) {
        if (block_phi[f] != None) {
          continue;
        }
        block_phi[f] = static_cast<u32>(acc.size());
        // an entry block that is a loop header also has the edge from outside
        // the function, its operand stays LiveOnEntry
        const u32 n_ops =
            static_cast<u32>(cfg.bbrs[f].pred.size()) + (f == cfg.entry);
        auto &phi = acc.emplace_back();
        phi.kind = Kind::Phi;
        phi.block = f;
        phi.ops_begin = static_cast<u32>(operands.size());
        phi.n_ops = n_ops;
        operands.resize(operands.size() + n_ops, LiveOnEntry);
        if (has_def[f] == 0) {
          work.push_back(f);
        }
      }
    }
  }

  // rename in dominator tree order, a block only needs the state at the end of
  // its immediate dominator
  TVec<u32> out_state(n_bbs, LiveOnEntry);
  TVec<u32> order;
  {
    TVec<u32> child_count(n_bbs + 1, 0);
    for (u32 b = 0; b < n_bbs; b++) {
      if (b != cfg.entry && dom.is_reachable(b)) {
        child_count[dom.idom(b) + 1]++;
      }
    }
    for (u32 b = 0; b < n_bbs; b++) {
      child_count[b + 1] += child_count[b];
    }
    TVec<u32> children(child_count[n_bbs]);
    TVec<u32> cursor(child_count.begin(), child_count.end() - 1);
    for (u32 b = 0; b < n_bbs; b++) {
      if (b != cfg.entry && dom.is_reachable(b)) {
        children[cursor[dom.idom(b)]++] = b;
      }
    }
    order.push_back(cfg.entry);
    for (size_t i = 0; i < order.size(); i++) {
      for (u32 c = child_count[order[i]]; c < child_count[order[i] + 1]; c++) {
        order.push_back(children[c]);
      }
    }
  }
  for (const u32 b : order) {
    u32 cur = b == cfg.entry ? LiveOnEntry : out_state[dom.idom(b)];
    if (block_phi[b] != None) {
      cur = block_phi[b];
    }
    for (const u32 i : per_block[b]) {
      const auto &a = all[i];
      const u32 id = static_cast<u32>(acc.size());
      auto &n = acc.emplace_back();
      n.kind = is_def(a.kind) ? Kind::Def : Kind::Use;
      n.access_kind = a.kind;
      n.ordered = a.ordered;
      n.block = b;
      n.instr = a.instr;
      n.defining = cur;
      index.insert({a.instr, id});
      if (n.kind == Kind::Def) {
        cur = id;
      }
    }
    out_state[b] = cur;
  }
  // phi operands, every edge of a reachable block
  for (u32 b = 0; b < n_bbs; b++) {
    if (!dom.is_reachable(b)) {
      continue;
    }
    for (const u32 s : cfg.bbrs[b].succ) {
      if (block_phi[s] == None) {
        continue;
      }
      const auto &preds = cfg.bbrs[s].pred;
      for (u32 i = 0; i < preds.size(); i++) {
        if (preds[i] == b) {
          operands[acc[block_phi[s]].ops_begin + i] = out_state[b];
        }
      }
    }
  }
  remove_trivial_phis();
}

void MemorySSA::remove_trivial_phis() {
  // repl[x] = what a removed phi was replaced by
  TVec<u32> repl(acc.size(), None);
  const auto resolve = [&](u32 x) {
    while (repl[x] != None) {
      x = repl[x];
    }
    return x;
  };
  bool changed = true;
  while (changed) {
    changed = false;
    for (u32 id = 0; id < acc.size(); id++) {
      if (acc[id].kind != Kind::Phi) {
        continue;
      }
      const u32 n = n_operands(id);
      u32 same = None;
      bool trivial = true;
      for (u32 i = 0; i < n && trivial; i++) {
        const u32 o = resolve(operands[acc[id].ops_begin + i]);
        if (o == id || o == same) {
          continue;
        }
        if (same != None) {
          trivial = false;
        }
        same = o;
      }
      if (!trivial) {
        continue;
      }
      // a phi without operands but itself is not reachable
      repl[id] = same == None ? LiveOnEntry : same;
      acc[id].kind = Kind::Removed;
      block_phi[acc[id].block] = None;
      changed = true;
    }
  }
  if (std::ranges::all_of(repl, [](u32 r) { return r == None; })) {
    return;
  }
  for (auto &a : acc) {
    if (a.kind == Kind::Def || a.kind == Kind::Use) {
      a.defining = resolve(a.defining);
    }
  }
  for (u32 id = 0; id < acc.size(); id++) {
    if (acc[id].kind == Kind::Phi) {
      for (u32 i = 0; i < n_operands(id); i++) {
        auto &o = operands[acc[id].ops_begin + i];
        o = resolve(o);
      }
    }
  }
}

u32 MemorySSA::access_of(fir::Instr instr) const {
  auto it = index.find(instr);
  return it == index.end() ? None : it->second;
}

u32 MemorySSA::defining_access(fir::Instr instr) const {
  const u32 id = access_of(instr);
  return id == None ? None : acc[id].defining;
}

bool MemorySSA::verify(const CFG &cfg, const Dominators &dom) const {
  const auto state_block_dominates = [&](u32 state, u32 use_block) {
    if (state == LiveOnEntry) {
      return true;
    }
    return dom.dominates(acc[state].block, use_block);
  };
  for (u32 id = 1; id < acc.size(); id++) {
    const auto &a = acc[id];
    switch (a.kind) {
      case Kind::Def:
      case Kind::Use: {
        if (a.defining >= acc.size() || a.defining == id ||
            acc[a.defining].kind == Kind::Use ||
            acc[a.defining].kind == Kind::Removed ||
            !state_block_dominates(a.defining, a.block)) {
          return false;
        }
        break;
      }
      case Kind::Phi: {
        const auto &preds = cfg.bbrs[a.block].pred;
        if (n_operands(id) != preds.size() + (a.block == cfg.entry) ||
            block_phi[a.block] != id) {
          return false;
        }
        for (u32 i = 0; i < preds.size(); i++) {
          const u32 o = operand(id, i);
          if (o >= acc.size() || acc[o].kind == Kind::Use ||
              acc[o].kind == Kind::Removed) {
            return false;
          }
          // the state has to be available at the end of the predecessor
          if (dom.is_reachable(preds[i]) &&
              !state_block_dominates(o, preds[i])) {
            return false;
          }
        }
        // phis that merge one state are removed after the build
        u32 same = None;
        bool trivial = true;
        for (u32 i = 0; i < n_operands(id); i++) {
          const u32 o = operand(id, i);
          if (o != id && o != same) {
            trivial = same == None;
            same = o;
          }
        }
        if (trivial) {
          return false;
        }
        break;
      }
      case Kind::LiveOnEntry:
      case Kind::Removed:
        break;
    }
  }
  return true;
}

bool MemorySSA::equivalent(const MemorySSA &o) const {
  if (acc.size() != o.acc.size()) {
    return false;
  }
  for (u32 id = 0; id < acc.size(); id++) {
    const auto &a = acc[id];
    const auto &b = o.acc[id];
    if (a.kind != b.kind || a.block != b.block || a.defining != b.defining ||
        a.instr != b.instr) {
      return false;
    }
    if (a.kind == Kind::Phi) {
      const u32 n = n_operands(id);
      if (n != o.n_operands(id)) {
        return false;
      }
      for (u32 i = 0; i < n; i++) {
        if (operand(id, i) != o.operand(id, i)) {
          return false;
        }
      }
    }
  }
  return true;
}

std::string MemorySSA::to_string(const CFG &cfg,
                                 MemorySSAWalker *walker) const {
  std::string out;
  const auto line = [&](const Access &a, u32 id) {
    switch (a.kind) {
      case Kind::Def:
        out += fmt::format("  ; {} = MemoryDef({})  {:d}\n", id, a.defining,
                           a.instr);
        break;
      case Kind::Use:
        if (walker != nullptr && a.access_kind == MemAccess::Kind::Load &&
            !a.ordered) {
          out += fmt::format("  ; MemoryUse({}) clobber={}  {:d}\n", a.defining,
                             walker->clobber(a.instr), a.instr);
        } else {
          out += fmt::format("  ; MemoryUse({})  {:d}\n", a.defining, a.instr);
        }
        break;
      case Kind::Phi: {
        out += fmt::format("  ; {} = MemoryPhi(", id);
        for (u32 i = 0; i < n_operands(id); i++) {
          const auto &preds = cfg.bbrs[a.block].pred;
          out += fmt::format(
              "{} [{}]{}", operand(id, i),
              i < preds.size() ? fmt::format("bb{}", preds[i]) : "entry",
              i + 1 < n_operands(id) ? ", " : "");
        }
        out += ")\n";
        break;
      }
      case Kind::LiveOnEntry:
      case Kind::Removed:
        break;
    }
  };
  out += "; 0 = LiveOnEntry\n";
  for (u32 b = 0; b < cfg.bbrs.size(); b++) {
    out += fmt::format("bb{}:\n", b);
    if (block_phi[b] != None) {
      line(acc[block_phi[b]], block_phi[b]);
    }
    for (const auto instr : cfg.bbrs[b].bb->instructions) {
      const u32 id = access_of(instr);
      if (id != None) {
        line(acc[id], id);
      }
    }
  }
  return out;
}

u32 MemorySSAWalker::walk(u32 a, Query &q) {
  while (true) {
    if (steps >= max_steps || a == q.stop) {
      return a;
    }
    steps++;
    const auto &acc = (*mssa)[a];
    switch (acc.kind) {
      case MemorySSA::Kind::LiveOnEntry:
      case MemorySSA::Kind::Use:
      case MemorySSA::Kind::Removed:
        return a;
      case MemorySSA::Kind::Def: {
        const auto mr = aq->mod_ref(acc.instr, *q.loc, q.meta);
        if ((static_cast<u8>(mr) & static_cast<u8>(q.want)) != 0) {
          return a;
        }
        a = acc.defining;
        break;
      }
      case MemorySSA::Kind::Phi: {
        // a phi that is walked already contributes nothing again (a cycle), one
        // that is finished gives the same answer on every path to it
        if (const auto it = std::ranges::find(q.active, a);
            it != q.active.end()) {
          q.cycle_to =
              std::min(q.cycle_to, static_cast<u32>(it - q.active.begin()));
          return MemorySSA::None;
        }
        if (const auto it = q.done.find(a); it != q.done.end()) {
          return it->second;
        }
        const u32 depth = static_cast<u32>(q.active.size());
        q.active.push_back(a);
        const u32 outer_cycle = q.cycle_to;
        q.cycle_to = ~0U;
        u32 result = MemorySSA::None;
        const u32 n = mssa->n_operands(a);
        for (u32 i = 0; i < n; i++) {
          const u32 r = walk(mssa->operand(a, i), q);
          if (r == MemorySSA::None) {
            continue;
          }
          if (result == MemorySSA::None) {
            result = r;
          } else if (result != r) {
            result = a;
            break;
          }
        }
        q.active.pop_back();
        // only a cycle back to a phi above this one makes the answer depend on
        // the path taken to get here
        if (q.cycle_to >= depth) {
          q.done.emplace(a, result);
          q.cycle_to = outer_cycle;
        } else {
          q.cycle_to = std::min(q.cycle_to, outer_cycle);
        }
        return result;
      }
    }
  }
}

u32 MemorySSAWalker::clobber_from(u32 start, const MemLoc &loc,
                                  const fir::AccessMeta *meta, ModRef want,
                                  u32 stop) {
  steps = 0;
  Query q{.loc = &loc,
          .meta = meta,
          .want = want,
          .stop = stop,
          .active = {},
          .done = {}};
  const u32 r = walk(start, q);
  return r == MemorySSA::None ? start : r;
}

u32 MemorySSAWalker::clobber(fir::Instr use) {
  const u32 id = mssa->access_of(use);
  ASSERT(id != MemorySSA::None);
  const auto &a = (*mssa)[id];
  const auto *m = info->find(use);
  if (a.ordered || m == nullptr || m->kind != MemAccess::Kind::Load) {
    return a.defining;
  }
  const auto *meta = use->has_access_meta
                         ? use->get_parent()->get_parent()->get_access_meta(use)
                         : nullptr;
  return clobber_from(a.defining, m->loc, meta);
}

bool MemorySSAWalker::clobbered_between(u32 from, u32 to, const MemLoc &loc,
                                        const fir::AccessMeta *meta,
                                        ModRef want) {
  if (from == to) {
    return false;
  }
  return clobber_from(to, loc, meta, want, from) != from;
}

}  // namespace foptim::optim
