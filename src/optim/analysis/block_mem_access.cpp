#include "block_mem_access.hpp"

#include <algorithm>

#include "ir/basic_block.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"

namespace foptim::optim {

namespace {
// addr = base + offset, the offset being the constant part of the address
void split_addr(fir::ValueR addr, fir::ValueR &base, i128 &offset) {
  base = addr;
  offset = 0;
  while (base.is_instr()) {
    auto i = base.as_instr();
    if (!i->is(fir::BinaryInstrSubType::PtrAdd) || !i->args[1].is_constant() ||
        !i->args[1].as_constant()->is_int()) {
      return;
    }
    offset += i->args[1].as_constant()->as_int();
    base = i->args[0];
  }
}
} // namespace

BlockMemAccesses::BlockMemAccesses(fir::BasicBlock bb) {
  using Kind = BlockMemAccess::Kind;
  const auto &instrs = bb->instructions;
  for (u32 i = 0; i < instrs.size(); i++) {
    auto instr = instrs[i];
    BlockMemAccess a{.kind = Kind::Load,
                     .pos = i,
                     .epoch = barriers,
                     .addr = {},
                     .size = 0,
                     .base = {},
                     .offset = 0,
                     .instr = instr};
    bool ordered = instr->Atomic || instr->Volatile;
    if (instr->is(fir::InstrType::LoadInstr) && !ordered) {
      a.kind = Kind::Load;
      a.addr = instr->args[0];
      a.size = instr.result_type()->get_size();
    } else if (instr->is(fir::InstrType::StoreInstr) && !ordered) {
      a.kind = Kind::Store;
      a.addr = instr->args[0];
      a.size = instr.result_type()->get_size();
    } else if (instr->is(fir::InstrType::LoadInstr) ||
               instr->is(fir::InstrType::StoreInstr) ||
               instr->pot_modifies_mem()) {
      a.kind = Kind::BarrierRW;
      barriers++;
    } else if (instr->pot_reads_mem()) {
      a.kind = Kind::BarrierR;
      barriers++;
    } else {
      continue;
    }
    if (a.kind == Kind::Load || a.kind == Kind::Store) {
      split_addr(a.addr, a.base, a.offset);
      index.insert({instr.result(), static_cast<u32>(acc.size())});
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
  using Kind = BlockMemAccess::Kind;
  using AAResult = AliasAnalyis::AAResult;
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
      if (m.base == c.base) {
        // same base: exact overlap test of the constant offsets
        if (m.offset < c.offset + c.size && c.offset < m.offset + m.size) {
          return MemDep::Dependent;
        }
        continue;
      }
      if (aa.alias(m.addr, c.addr, m.size, c.size) != AAResult::NoAlias) {
        return MemDep::Dependent;
      }
    }
  }
  return MemDep::Independent;
}

} // namespace foptim::optim
