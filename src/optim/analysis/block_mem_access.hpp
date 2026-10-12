#pragma once
#include <span>

#include "ir/basic_block_ref.hpp"
#include "ir/value.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "utils/map.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

// Answer of a memory dependence query. Meant to grow (needs runtime check,
// conflicting) without changing the callers, so do not use a bool.
enum class MemDep : u8 {
  Independent,
  Dependent,
};

struct BlockMemAccess {
  enum class Kind : u8 {
    Load,
    Store,
    // reads and writes unknown memory (calls, fences, volatile / atomic
    // accesses, ...), nothing may be moved across it
    BarrierRW,
    // only reads unknown memory, loads can be moved across it, stores not
    BarrierR,
  };
  Kind kind;
  // index of the instruction in the block when the snapshot was taken
  u32 pos;
  // number of barriers before this access
  u32 epoch;
  // only for Load / Store
  fir::ValueR addr;
  u32 size;
  // addr = base + offset, the offset is the sum of the constants of a chain of
  // "PtrAdd(x, const)" on top of "base". Two accesses with the same base are
  // compared exactly.
  fir::ValueR base;
  i128 offset;
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

} // namespace foptim::optim
