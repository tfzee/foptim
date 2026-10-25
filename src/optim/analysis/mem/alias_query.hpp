#pragma once
#include "ir/access_meta.hpp"
#include "ir/instruction.hpp"
#include "optim/analysis/mem/mem_access.hpp"
#include "optim/analysis/mem/provenance.hpp"
#include "utils/types.hpp"

namespace foptim::optim {

enum class AliasResult : u8 {
  // the byte ranges are disjoint
  No,
  May,
  // the ranges overlap but are not the same
  Partial,
  // both start at the same address
  Must,
};

// what an instruction may do to a memory location
enum class ModRef : u8 {
  NoModRef = 0,
  Ref = 1,
  Mod = 2,
  ModRef = 3,
};

constexpr bool may_read(ModRef m) { return (static_cast<u8>(m) & 1) != 0; }
constexpr bool may_write(ModRef m) { return (static_cast<u8>(m) & 2) != 0; }

/*
  The one place that answers alias and mod/ref questions of a function. Two
  locations with the same base and variable part are compared by offset and
  size, different bases by their alias sets, everything else may alias. A
  location whose base is definitely null never aliases anything (accessing null
  is undefined behavior). Size 0 is an unknown size.
*/
class AliasQuery {
 public:
  explicit AliasQuery(Provenance &prov) : prov(&prov) {}

  // With across_iterations a and b may be from different loop iterations, so
  // locations sharing a variable index are only compared by their base.
  AliasResult alias(const MemLoc &a, const MemLoc &b,
                    bool across_iterations = false);
  AliasResult alias(fir::ValueR a, u32 a_size, fir::ValueR b, u32 b_size,
                    bool across_iterations = false);

  // The metadata can prove accesses apart that the addresses say may overlap
  // (incompatible types never access the same bytes). Null is unknown. Scopes
  // are ignored across_iterations, they belong to one instance of the code.
  AliasResult alias(const MemLoc &a, const fir::AccessMeta *ma, const MemLoc &b,
                    const fir::AccessMeta *mb, bool across_iterations = false);
  // two loads or stores
  AliasResult alias(fir::Instr a, fir::Instr b, bool across_iterations = false);
  [[nodiscard]] bool meta_disjoint(const fir::AccessMeta *a,
                                   const fir::AccessMeta *b,
                                   bool use_scopes) const;

  // what instr may do to the bytes of loc, meta is the metadata of loc
  ModRef mod_ref(fir::Instr instr, const MemLoc &loc,
                 const fir::AccessMeta *meta = nullptr);

  [[nodiscard]] Provenance &provenance() const { return *prov; }

 private:
  ModRef call_mod_ref(fir::Instr call, const MemLoc &loc);
  ModRef access_mod_ref(fir::ValueR ptr, u32 size, const MemLoc &loc,
                        ModRef kind, const fir::AccessMeta *own = nullptr,
                        const fir::AccessMeta *meta = nullptr);

  Provenance *prov;
};

}  // namespace foptim::optim
