#include "block_mem_access.hpp"

#include <algorithm>

#include "ir/basic_block.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"

namespace foptim::optim {

using Kind = MemAccess::Kind;

BlockMemAccesses::BlockMemAccesses(fir::BasicBlock bb) {
  const auto &instrs = bb->instructions;
  for (u32 i = 0; i < instrs.size(); i++) {
    auto instr = instrs[i];
    auto m = classify_access(instr);
    if (!m) {
      continue;
    }
    BlockMemAccess a{.kind = m->kind,
                     .pos = i,
                     .epoch = barriers,
                     .addr = {},
                     .size = 0,
                     .loc = {},
                     .instr = instr};
    if (a.kind == Kind::Load || a.kind == Kind::Store) {
      a.addr = instr->args[0];
      a.size = m->loc.size;
      a.loc = m->loc;
      index.insert({instr.result(), static_cast<u32>(acc.size())});
    } else {
      barriers++;
    }
    acc.push_back(a);
  }
}

std::optional<u32> BlockMemAccesses::index_of(fir::Instr instr) const {
  auto it = index.find(instr.result());
  if (it == index.end()) {
    return std::nullopt;
  }
  return it->second;
}

MemDep BlockMemAccesses::can_sink(std::span<const u32> members,
                                  AliasAnalyis &aa) const {
  if (members.size() < 2) {
    return MemDep::Independent;
  }
  auto kind = acc[members[0]].kind;
  if (kind != Kind::Load && kind != Kind::Store) {
    return MemDep::Dependent;
  }
  TVec<u32> sorted(members.begin(), members.end());
  std::ranges::sort(sorted);
  for (auto m : sorted) {
    if (acc[m].kind != kind) {
      return MemDep::Dependent;
    }
  }
  if (std::ranges::adjacent_find(sorted) != sorted.end()) {
    return MemDep::Dependent;
  }
  auto first = sorted.front();
  auto last = sorted.back();
  if (last - first > MaxScan) {
    return MemDep::Dependent;
  }

  // walk the accesses between the first and the last member, "n_before" is
  // the number of members that are in front of the current access (only
  // those are moved across it)
  size_t n_before = 1;
  for (u32 i = first + 1; i < last; i++) {
    if (n_before < sorted.size() && sorted[n_before] == i) {
      n_before++;
      continue;
    }
    const auto &c = acc[i];
    switch (c.kind) {
      case Kind::BarrierRW:
        return MemDep::Dependent;
      case Kind::BarrierR:
        if (kind == Kind::Store) {
          return MemDep::Dependent;
        }
        continue;
      case Kind::Load:
        if (kind == Kind::Load) {
          continue;
        }
        break;
      case Kind::Store:
        break;
    }
    for (size_t j = 0; j < n_before; j++) {
      const auto &m = acc[sorted[j]];
      if (access_dep(aa, m.instr, c.instr) != MemDep::Independent) {
        return MemDep::Dependent;
      }
    }
  }
  return MemDep::Independent;
}

}  // namespace foptim::optim
