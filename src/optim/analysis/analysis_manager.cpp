#include "optim/analysis/analysis_manager.hpp"

#include "ir/context.hpp"
#include "utils/todo.hpp"

namespace foptim::fir {
void AnalysisCacheDeleter::operator()(optim::FunctionAnalysisCache *p) const {
  delete p;
}
} // namespace foptim::fir

namespace foptim::optim {

namespace {
FunctionAnalysisCache &get_cache(fir::Function &func) {
  if (!func.analysis_cache) {
    func.analysis_cache.reset(new FunctionAnalysisCache{});
  }
  return *func.analysis_cache;
}
} // namespace

CFG &AnalysisManager::cfg(fir::Function &func) {
  auto &cache = get_cache(func);
  if (!cache.cfg) {
    cache.cfg = std::make_unique<CFG>(func);
  }
  return *cache.cfg;
}

Dominators &AnalysisManager::dom(fir::Function &func) {
  auto &c = cfg(func);
  auto &cache = get_cache(func);
  if (!cache.dom) {
    cache.dom = std::make_unique<Dominators>(c);
  }
  return *cache.dom;
}

ConstraintAnalysis &AnalysisManager::constraints(fir::Function &func) {
  auto &c = cfg(func);
  auto &cache = get_cache(func);
  if (!cache.constraints) {
    cache.constraints = std::make_unique<ConstraintAnalysis>(c);
  }
  return *cache.constraints;
}

void AnalysisManager::invalidate(fir::Function &func, PreservedAnalysis pres) {
  if (!func.analysis_cache) {
    return;
  }
  auto &cache = *func.analysis_cache;
#ifdef ASSERT_ENABLED
  verify_preserved(func, pres);
#endif
  // dependents first, they point into the CFG
  if (!pres.preserves(AnalysisKind::Constraints) ||
      !pres.preserves(AnalysisKind::CFG)) {
    cache.constraints.reset();
  }
  if (!pres.preserves(AnalysisKind::Dominators) ||
      !pres.preserves(AnalysisKind::CFG)) {
    cache.dom.reset();
  }
  if (!pres.preserves(AnalysisKind::CFG)) {
    cache.cfg.reset();
  }
}

void AnalysisManager::invalidate_all(fir::Context &ctx) {
  for (auto *func : ctx->storage.functions.all()) {
    func->analysis_cache.reset();
  }
}

void AnalysisManager::verify_preserved(fir::Function &func,
                                       PreservedAnalysis pres) {
  if (!func.analysis_cache || func.is_decl()) {
    return;
  }
  auto &cache = *func.analysis_cache;
  if (cache.cfg && pres.preserves(AnalysisKind::CFG)) {
    const CFG fresh{func};
    const auto &old = *cache.cfg;
    const bool same =
        old.entry == fresh.entry && old.bbrs.size() == fresh.bbrs.size() &&
        std::ranges::all_of(std::views::iota(size_t{0}, fresh.bbrs.size()),
                            [&](size_t i) {
                              const auto &a = old.bbrs[i];
                              const auto &b = fresh.bbrs[i];
                              return a.bb == b.bb &&
                                     std::ranges::equal(a.pred, b.pred) &&
                                     std::ranges::equal(a.succ, b.succ);
                            });
    if (!same) {
      fmt::println(stderr,
                   "AnalysisManager: pass claimed to preserve the CFG of '{}' "
                   "but it changed",
                   func.name.c_str());
    }
    ASSERT_M(same, "Pass returned wrong PreservedAnalysis (CFG)");
    if (cache.dom && pres.preserves(AnalysisKind::Dominators)) {
      const Dominators fresh_dom{fresh};
      bool dom_same = true;
      for (u32 i = 0; i < fresh.bbrs.size(); i++) {
        dom_same = dom_same && fresh_dom.idom(i) == cache.dom->idom(i) &&
                   fresh_dom.is_reachable(i) == cache.dom->is_reachable(i);
      }
      ASSERT_M(dom_same, "Pass returned wrong PreservedAnalysis (Dominators)");
    }
  }
}

} // namespace foptim::optim
