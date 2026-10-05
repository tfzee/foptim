#include "optim/analysis/analysis_manager.hpp"

#include "ir/context.hpp"
#include "utils/todo.hpp"
#include <fmt/base.h>

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
  ASSERT(verify_preserved(func, pres));
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

bool AnalysisManager::verify_preserved(fir::Function &func,
                                       PreservedAnalysis pres) {
  if (!func.analysis_cache || func.is_decl()) {
    return true;
  }
  auto &cache = *func.analysis_cache;
  if (cache.cfg && pres.preserves(AnalysisKind::CFG)) {
    const CFG fresh{func};
    const auto &old = *cache.cfg;
    bool same =
        old.entry == fresh.entry && old.bbrs.size() == fresh.bbrs.size();
    for (size_t i = 0; same && i < fresh.bbrs.size(); i++) {
      const auto &a = old.bbrs[i];
      const auto &b = fresh.bbrs[i];
      same = a.bb == b.bb && a.pred.size() == b.pred.size() &&
             a.succ.size() == b.succ.size();
      for (size_t j = 0; same && j < a.pred.size(); j++) {
        same = a.pred[j] == b.pred[j];
      }
      for (size_t j = 0; same && j < a.succ.size(); j++) {
        same = a.succ[j] == b.succ[j];
      }
    }
    if (!same) {
      fmt::println(stderr,
                   "AnalysisManager: pass claimed to preserve the CFG of '{}' "
                   "but it changed",
                   func.name.c_str());
    }
    if (!same) {
      fmt::println("Pass returned wrong PreservedAnalysis (CFG)");
      return false;
    }
    if (cache.dom && pres.preserves(AnalysisKind::Dominators)) {
      const Dominators fresh_dom{fresh};
      bool dom_same = true;
      for (u32 i = 0; i < fresh.bbrs.size(); i++) {
        dom_same = dom_same && fresh_dom.idom(i) == cache.dom->idom(i) &&
                   fresh_dom.is_reachable(i) == cache.dom->is_reachable(i);
      }
      if (!dom_same) {
        fmt::println("Pass returned wrong PreservedAnalysis (Dominators)");
        return false;
      }
    }
  }
  return true;
}

} // namespace foptim::optim
