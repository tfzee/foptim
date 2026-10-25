#pragma once
#include <array>
#include <optional>
#include <span>

#include "ir/basic_block_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/value.hpp"
#include "utils/map.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

// index * scale, added to the base of an address
struct IndexTerm {
  fir::ValueR index;
  i64 scale;
  bool operator==(const IndexTerm &) const = default;
};

/*
  An address as base + offset + sum(index * scale). The offset is the sum of
  all constants of the chain of PtrAdds on top of the base, the terms are the
  variable indices (merged when the same value is added several times, in no
  fixed order, compare them with same_terms). Arithmetic is modular 64 bit, a
  chain that would overflow stops early and stays in the base. Only i64
  values are looked through (add, sub, mul / shl by a constant), a narrower
  index is one opaque term.
*/
struct PtrShape {
  static constexpr u32 MaxTerms = 3;

  fir::ValueR addr;
  fir::ValueR base;
  i64 offset = 0;
  std::array<IndexTerm, MaxTerms> terms_{};
  u8 n_terms = 0;
  // addr = anchor + anchor_offset, anchor is the existing pointer value with
  // only the pure constant PtrAdds on top removed
  fir::ValueR anchor;
  i64 anchor_offset = 0;

  [[nodiscard]] std::span<const IndexTerm> terms() const {
    return {terms_.data(), n_terms};
  }
  [[nodiscard]] bool has_terms() const { return n_terms != 0; }
  [[nodiscard]] bool same_base(const PtrShape &o) const;
  [[nodiscard]] bool same_terms(const PtrShape &o) const;
  bool operator==(const PtrShape &o) const {
    return same_base(o) && offset == o.offset;
  }
};

PtrShape decompose_ptr(fir::ValueR addr);

struct MemLoc {
  PtrShape ptr;
  // 0 if unknown
  u32 size = 0;

  // overlap of the byte ranges if both have the same base and variable part,
  // nullopt if only an alias analysis can tell
  [[nodiscard]] std::optional<bool> overlaps(const MemLoc &o) const;
};

struct MemAccess {
  enum class Kind : u8 {
    Load,
    Store,
    // reads and writes unknown memory (calls, fences, volatile / atomic
    // accesses, ...), nothing may be moved across it
    BarrierRW,
    // only reads unknown memory, loads can be moved across it, stores not
    BarrierR,
  };
  Kind kind = Kind::Load;
  fir::Instr instr;
  // only meaningful for Load / Store and for volatile / atomic accesses
  MemLoc loc;
  // volatile or atomic load / store, is always a barrier
  bool ordered = false;

  [[nodiscard]] bool is_load_store() const {
    return kind == Kind::Load || kind == Kind::Store;
  }
};

// nullopt for instructions that do not touch memory
std::optional<MemAccess> classify_access(fir::Instr instr);

/*
  All memory accesses of a function in program order. Cached by the
  AnalysisManager and dropped by every pass that changed anything.
*/
class MemAccessInfo {
 public:
  explicit MemAccessInfo(fir::Function &func);

  [[nodiscard]] const FVec<MemAccess> &accesses() const { return acc; }
  [[nodiscard]] const MemAccess *find(fir::Instr instr) const;

  [[nodiscard]] bool equivalent(const MemAccessInfo &o) const;

 private:
  FVec<MemAccess> acc;
  FDMap<fir::ValueR, u32> index;
};

}  // namespace foptim::optim
