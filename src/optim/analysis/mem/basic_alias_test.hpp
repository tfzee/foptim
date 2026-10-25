#pragma once
#include <memory>

#include "ir/function.hpp"
#include "ir/value.hpp"
#include "utils/types.hpp"

namespace foptim::optim {

class Provenance;

// Thin wrapper over AliasQuery. With use_cache the Provenance comes from the
// AnalysisManager, otherwise the object owns one that reset() drops (module
// passes that change attributes while they ask).
struct AliasAnalyis {
  enum class AAResult : u8 {
    // both start at the same address
    Alias,
    MightAlias,
    NoAlias,
  };

  explicit AliasAnalyis(bool use_cache = true);
  ~AliasAnalyis();
  AliasAnalyis(const AliasAnalyis &) = delete;
  AliasAnalyis &operator=(const AliasAnalyis &) = delete;

  void reset();

  bool is_known_local_stack(fir::ValueR v);

  // size 0 means any size. across_iterations: a and b may be from different
  // iterations of a loop, see AliasQuery
  AAResult alias(fir::ValueR a, fir::ValueR b, size_t a_size = 0,
                 size_t b_size = 0, bool across_iterations = false);
  // two loads or stores, uses their metadata
  AAResult alias_access(fir::Instr a, fir::Instr b,
                        bool across_iterations = false);

 private:
  Provenance &get(fir::ValueR a, fir::ValueR b);
  Provenance &get(fir::Function *f);

  bool use_cache;
  fir::Function *bound = nullptr;
  std::unique_ptr<Provenance> owned;
};

}  // namespace foptim::optim
