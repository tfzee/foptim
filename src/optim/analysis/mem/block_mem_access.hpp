#pragma once
#include <span>

#include "ir/basic_block_ref.hpp"
#include "ir/value.hpp"
#include "optim/analysis/mem/basic_alias_test.hpp"
#include "optim/analysis/mem/mem_access.hpp"
#include "optim/analysis/mem/mem_dep.hpp"
#include "utils/map.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

struct BlockMemAccess {
  using Kind = MemAccess::Kind;
  Kind kind;
  // index of the instruction in the block when the snapshot was taken
  u32 pos;
  // number of barriers before this access
  u32 epoch;
  // only for Load / Store, addr = loc.ptr.base + offset + terms
  fir::ValueR addr;
  u32 size;
  MemLoc loc;
  // may be erased after the snapshot was taken, never read it through here
  // after transformations, only "kind", "addr" and "size" stay meaningful
  fir::Instr instr;
};

/*
  Snapshot of all memory operations of one basic block, in program order.

  It answers what the SLP (and later the loop) vectorizer needs if you can sink
*/
class BlockMemAccesses {
 public:
  static constexpr u32 MaxScan = 256;

  explicit BlockMemAccesses(fir::BasicBlock bb);

  [[nodiscard]] const TVec<BlockMemAccess> &accesses() const { return acc; }
  [[nodiscard]] u32 n_barriers() const { return barriers; }

  // index into "accesses()" of a Load / Store instruction of this block
  [[nodiscard]] std::optional<u32> index_of(fir::Instr instr) const;

  // Can all "members" (indices into "accesses()", same kind, Load or Store, in
  // any order) be moved down to the position of the last of them?
  MemDep can_sink(std::span<const u32> members, AliasAnalyis &aa) const;

 private:
  TVec<BlockMemAccess> acc;
  TMap<fir::ValueR, u32> index;
  u32 barriers = 0;
};

}  // namespace foptim::optim
