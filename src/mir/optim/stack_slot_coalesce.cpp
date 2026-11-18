#include "mir/optim/stack_slot_coalesce.hpp"

#include <algorithm>
#include <bit>

#include "mir/analysis/cfg.hpp"
#include "mir/instr.hpp"
#include "utils/stats.hpp"

namespace foptim::fmir {
namespace {

using Bits = TVec<u64>;

// the n x n bit matrix gets too big for functions with this many slots
constexpr size_t MaxSlots = 12000;

bool test(const u64 *bits, size_t i) {
  return ((bits[i / 64] >> (i % 64)) & 1) != 0;
}
void set(u64 *bits, size_t i) { bits[i / 64] |= u64{1} << (i % 64); }
void clear(u64 *bits, size_t i) { bits[i / 64] &= ~(u64{1} << (i % 64)); }

struct SlotRoles {
  TVec<u32> reads;
  TVec<u32> writes;
  // operand indices that are a slot and have a known role
  u64 covered = 0;
  TVec<ArgData> tmp;

  void collect(const MInstr &instr) {
    reads.clear();
    writes.clear();
    covered = 0;
    tmp.clear();
    read_args(instr, tmp);
    for (auto &a : tmp) {
      if (a.arg.isStackSlot()) {
        reads.push_back(static_cast<u32>(a.arg.imm - 1));
        covered |= u64{1} << a.indx;
      }
    }
    tmp.clear();
    written_args(instr, tmp);
    for (auto &a : tmp) {
      if (a.arg.isStackSlot()) {
        writes.push_back(static_cast<u32>(a.arg.imm - 1));
        covered |= u64{1} << a.indx;
      }
    }
  }

  [[nodiscard]] bool reads_slot(u32 s) const {
    return std::ranges::find(reads, s) != reads.end();
  }
};

}  // namespace

void StackSlotCoalesce::apply(MFunc &func, const conf::CompConf & /*config*/) {
  const size_t n = func.extra_stack_slots.size();
  if (n < 2 || n > MaxSlots) {
    return;
  }
  const size_t words = (n + 63) / 64;
  const size_t nb = func.bbs.size();

  // a slot whose address is taken, that is accessed with another width or
  // whose access has no known role stays where it is
  TVec<u8> bad(n, 0);
  SlotRoles roles;
  for (auto &bb : func.bbs) {
    for (auto &instr : bb.instrs) {
      bool has_slot = false;
      for (size_t k = 0; k < instr.n_args; k++) {
        has_slot |= instr.args[k].isStackSlot();
      }
      if (!has_slot) {
        continue;
      }
      roles.collect(instr);
      for (size_t k = 0; k < instr.n_args; k++) {
        const auto &arg = instr.args[k];
        if (!arg.isStackSlot()) {
          continue;
        }
        const auto id = arg.imm - 1;
        if (arg.scale != func.extra_stack_slots[id].size ||
            instr.is(X86Subtype::lea) || ((roles.covered >> k) & 1) == 0) {
          bad[id] = 1;
        }
      }
    }
  }

  // upward exposed reads and full writes of every block
  Bits gen(nb * words, 0);
  Bits kill(nb * words, 0);
  for (size_t b = 0; b < nb; b++) {
    u64 *g = &gen[b * words];
    u64 *k = &kill[b * words];
    for (auto &instr : func.bbs[b].instrs) {
      roles.collect(instr);
      for (auto r : roles.reads) {
        if (!test(k, r)) {
          set(g, r);
        }
      }
      for (auto w : roles.writes) {
        if (!roles.reads_slot(w)) {
          set(k, w);
        }
      }
    }
  }

  const CFG cfg(func);
  Bits live_in(nb * words, 0);
  Bits live_out(nb * words, 0);
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t b = nb; b-- > 0;) {
      u64 *out = &live_out[b * words];
      u64 *in = &live_in[b * words];
      for (auto s : cfg.bbrs[b].succ) {
        const u64 *sin = &live_in[s * words];
        for (size_t w = 0; w < words; w++) {
          out[w] |= sin[w];
        }
      }
      for (size_t w = 0; w < words; w++) {
        const u64 val =
            gen[(b * words) + w] | (out[w] & ~kill[(b * words) + w]);
        if (val != in[w]) {
          in[w] = val;
          changed = true;
        }
      }
    }
  }

  // slots written while the other one is live
  Bits adj(n * words, 0);
  Bits live(words);
  for (size_t b = 0; b < nb; b++) {
    std::copy_n(&live_out[b * words], words, live.begin());
    auto &instrs = func.bbs[b].instrs;
    for (size_t i = instrs.size(); i-- > 0;) {
      roles.collect(instrs[i]);
      for (auto w : roles.writes) {
        u64 *row = &adj[w * words];
        for (size_t k = 0; k < words; k++) {
          row[k] |= live[k];
        }
        for (auto w2 : roles.writes) {
          set(row, w2);
        }
      }
      for (auto w : roles.writes) {
        if (!roles.reads_slot(w)) {
          clear(live.data(), w);
        }
      }
      for (auto r : roles.reads) {
        set(live.data(), r);
      }
    }
  }
  for (size_t i = 0; i < n; i++) {
    for (size_t w = 0; w < words; w++) {
      u64 bits = adj[(i * words) + w];
      while (bits != 0) {
        const size_t j = (w * 64) + static_cast<size_t>(std::countr_zero(bits));
        bits &= bits - 1;
        set(&adj[j * words], i);
      }
    }
  }

  // first fit, biggest slots first, so a smaller slot can live at the start of
  // a bigger representative (still naturally aligned). A representative owns
  // the union of what its members interfere with.
  TVec<u32> order(n);
  for (size_t i = 0; i < n; i++) {
    order[i] = static_cast<u32>(i);
  }
  std::ranges::stable_sort(order, [&](u32 a, u32 b) {
    return func.extra_stack_slots[a].size > func.extra_stack_slots[b].size;
  });
  TVec<u32> rep_of(n);
  TVec<u32> reps;
  size_t merged = 0;
  for (size_t i = 0; i < n; i++) {
    rep_of[i] = static_cast<u32>(i);
  }
  for (auto i : order) {
    if (bad[i] != 0 || func.extra_stack_slots[i].size == 0) {
      continue;
    }
    bool found = false;
    for (auto r : reps) {
      if (test(&adj[r * words], i)) {
        continue;
      }
      u64 *rrow = &adj[r * words];
      const u64 *irow = &adj[i * words];
      for (size_t w = 0; w < words; w++) {
        rrow[w] |= irow[w];
      }
      rep_of[i] = r;
      merged++;
      found = true;
      break;
    }
    if (!found) {
      reps.push_back(i);
    }
  }
  if (merged == 0) {
    return;
  }

  for (auto &bb : func.bbs) {
    for (auto &instr : bb.instrs) {
      for (size_t k = 0; k < instr.n_args; k++) {
        auto &arg = instr.args[k];
        if (arg.isStackSlot()) {
          arg.imm = rep_of[arg.imm - 1] + 1;
        }
      }
    }
  }
  for (size_t i = 0; i < n; i++) {
    if (rep_of[i] != i) {
      func.extra_stack_slots[i].size = 0;
    }
  }
  utils::StatCollector::get().addi(static_cast<i64>(merged),
                                   "StackSlotsMerged");
}

}  // namespace foptim::fmir
