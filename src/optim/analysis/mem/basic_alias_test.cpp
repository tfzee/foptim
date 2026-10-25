#include "basic_alias_test.hpp"

#include "ir/basic_block.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/mem/alias_query.hpp"

namespace foptim::optim {

namespace {
fir::Function *function_of(fir::ValueR v) {
  if (v.is_instr()) {
    return v.as_instr()->get_parent()->get_parent().func;
  }
  if (v.is_bb_arg()) {
    return v.as_bb_arg()->_parent->get_parent().func;
  }
  return nullptr;
}
}  // namespace

AliasAnalyis::AliasAnalyis(bool use_cache) : use_cache(use_cache) {}
AliasAnalyis::~AliasAnalyis() = default;

void AliasAnalyis::reset() {
  owned.reset();
  bound = nullptr;
}

Provenance &AliasAnalyis::get(fir::ValueR a, fir::ValueR b) {
  fir::Function *f = function_of(a);
  if (f == nullptr) {
    f = function_of(b);
  }
  return get(f);
}

Provenance &AliasAnalyis::get(fir::Function *f) {
  if (f != nullptr && use_cache) {
    return AnalysisManager::provenance(*f);
  }
  if (!owned || (f != nullptr && f != bound)) {
    if (f != nullptr) {
      owned = std::make_unique<Provenance>(*f);
      bound = f;
    } else {
      owned = std::make_unique<Provenance>();
    }
  }
  return *owned;
}

bool AliasAnalyis::is_known_local_stack(fir::ValueR v) {
  return get(v, v).is_stack(v);
}

AliasAnalyis::AAResult AliasAnalyis::alias(fir::ValueR a, fir::ValueR b,
                                           size_t a_size, size_t b_size,
                                           bool across_iterations) {
  ZoneScopedN("AliasAnalysis");
  if (a == b) {
    return AAResult::Alias;
  }
  AliasQuery q{get(a, b)};
  switch (q.alias(a, static_cast<u32>(a_size), b, static_cast<u32>(b_size),
                  across_iterations)) {
    case AliasResult::No:
      return AAResult::NoAlias;
    case AliasResult::Must:
      return AAResult::Alias;
    case AliasResult::May:
    case AliasResult::Partial:
      return AAResult::MightAlias;
  }
  return AAResult::MightAlias;
}

AliasAnalyis::AAResult AliasAnalyis::alias_access(fir::Instr a, fir::Instr b,
                                                  bool across_iterations) {
  ZoneScopedN("AliasAnalysis");
  AliasQuery q{get(a->get_parent()->get_parent().func)};
  switch (q.alias(a, b, across_iterations)) {
    case AliasResult::No:
      return AAResult::NoAlias;
    case AliasResult::Must:
      return AAResult::Alias;
    case AliasResult::May:
    case AliasResult::Partial:
      return AAResult::MightAlias;
  }
  return AAResult::MightAlias;
}

}  // namespace foptim::optim
