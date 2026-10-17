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
#ifdef ASSERT_ENABLED
  // ASSERT evaluates its argument in every build, this one rebuilds the CFG
  ASSERT(verify_preserved(func, pres));
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

namespace {
u64 type_id(fir::TypeR t) {
  return t.is_valid() ? reinterpret_cast<uintptr_t>(t.get_raw_ptr()) : 0;
}
u64 mix(u64 h, u64 v) {
  h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
  h *= 0xFF51AFD7ED558CCDULL;
  return h ^ (h >> 32);
}
} // namespace

void AnalysisManager::invalidate_all(fir::Context &ctx) {
  for (auto *func : ctx->storage.functions.all()) {
    func->analysis_cache.reset();
    if (func->clean_passes) {
      func->clean_passes->reset(false);
    }
  }
}


u64 AnalysisManager::hash_instr(fir::Instr instr) {
  const ankerl::unordered_dense::hash<fir::ValueR> hv{};
  u64 h = std::hash<fir::Instr>()(instr);
  h = mix(h, static_cast<u64>(instr->instr_type) << 32 | instr->subtype);
  h = mix(h, type_id(instr->result_type()));
  h = mix(h, type_id(instr->extra_type));
  h = mix(h, static_cast<u64>(instr->NSW) | instr->NUW << 1 |
                 instr->InBounds << 2 | instr->Volatile << 3 |
                 instr->Atomic << 4 | instr->Ordering << 5);
  h = mix(h, instr->args.size());
  for (const auto &a : instr->args) {
    h = mix(h, hv(a));
  }
  h = mix(h, instr->bbs.size());
  for (const auto &target : instr->bbs) {
    h = mix(h, std::hash<fir::BasicBlock>()(target.bb));
    h = mix(h, target.args.size());
    for (const auto &a : target.args) {
      h = mix(h, hv(a));
    }
  }
  return h;
}

u64 AnalysisManager::hash_function(const fir::Function &func) {
  u64 h = func.basic_blocks.size();
  for (const auto bb : func.basic_blocks) {
    h = mix(h, std::hash<fir::BasicBlock>()(bb));
    h = mix(h, bb->args.size());
    for (const auto arg : bb->args) {
      h = mix(h, std::hash<fir::BBArgument>()(arg));
      h = mix(h, type_id(arg->get_type()));
    }
    h = mix(h, bb->instructions.size());
    for (const auto instr : bb->instructions) {
      h = mix(h, hash_instr(instr));
    }
  }
  return h;
}

u64 AnalysisManager::hash_interface(const fir::Function &func) {
  const auto &a = func.attribs;
  u64 h = mix(type_id(func.func_ty), func.is_decl());
  h = mix(h, static_cast<u64>(a.cc) | static_cast<u64>(a.linkage) << 8 |
                 static_cast<u64>(a.linkvis) << 16 |
                 static_cast<u64>(a.min_align) << 24);
  h = mix(h, static_cast<u64>(a.variadic) | a.must_progress << 1 |
                 a.must_return << 2 | a.no_recurse << 3 | a.no_inline << 4 |
                 a.no_return << 5 | a.cold << 6 | a.must_inline << 7 |
                 a.mem_write_only << 8 | a.mem_read_only << 9 |
                 a.mem_read_none << 10 | a.maybe_can_wfvec << 11);
  return h;
}

namespace {
void drop_bits(fir::Function &f) {
  if (f.clean_passes) {
    f.clean_passes->reset(false);
  }
}
void drop_bits_of_users(const IRVec<fir::Use> &uses) {
  for (const auto &use : uses) {
    if (use.user.is_valid() && use.user->get_parent().is_valid()) {
      drop_bits(*use.user->get_parent()->get_parent().func);
    }
  }
}
} // namespace

void AnalysisManager::touched_body(fir::Function &f) {
  std::lock_guard<std::mutex> lock{report_mutex};
  f.analysis_cache.reset();
  drop_bits(f);
  reported_bodies.insert(&f);
}

void AnalysisManager::touched_interface(fir::Function &g) {
  std::lock_guard<std::mutex> lock{report_mutex};
  drop_bits(g);
  drop_bits_of_users(g.get_uses());
  reported_interfaces.insert(&g);
}

void AnalysisManager::touched_callers(fir::Function &f) {
  for (const auto &use : f.get_uses()) {
    if (use.user.is_valid() && use.user->get_parent().is_valid()) {
      touched_body(*use.user->get_parent()->get_parent().func);
    }
  }
}

void AnalysisManager::touched_signature(fir::Function &f) {
  touched_body(f);
  touched_interface(f);
  touched_callers(f);
}

void AnalysisManager::touched_global(fir::Global g) {
  std::lock_guard<std::mutex> lock{report_mutex};
  drop_bits_of_users(g->get_uses());
}

void AnalysisManager::begin_module_pass([[maybe_unused]] fir::Context &ctx) {
  reported_bodies.clear();
  reported_interfaces.clear();
#ifdef HASH_CHECKS_ENABLED
  before_module_pass.clear();
  for (auto *f : ctx->storage.functions.all()) {
    before_module_pass.insert(
        {f, Snapshot{.body = f->is_decl() ? 0 : hash_function(*f),
                     .interface = hash_interface(*f)}});
  }
#endif
}

bool AnalysisManager::end_module_pass(fir::Context &ctx,
                                      PreservedAnalysis result,
                                      std::string_view pass_name) {
  const bool reported = result.mask == PreservedAnalysis::all().mask;
  if (!reported) {
    invalidate_all(ctx);
  }
#ifdef HASH_CHECKS_ENABLED
  if (reported) {
    for (auto *f : ctx->storage.functions.all()) {
      auto before = before_module_pass.find(f);
      if (before == before_module_pass.end()) {
        continue;
      }
      if (!f->is_decl() && hash_function(*f) != before->second.body &&
          !reported_bodies.contains(f)) {
        fmt::println(stderr,
                     "AnalysisManager: module pass '{}' returned "
                     "PreservedAnalysis::all() but changed the body of '{}' "
                     "without touched_body",
                     pass_name, f->name.c_str());
        ASSERT(false);
      }
      if (hash_interface(*f) != before->second.interface &&
          !reported_interfaces.contains(f)) {
        fmt::println(stderr,
                     "AnalysisManager: module pass '{}' returned "
                     "PreservedAnalysis::all() but changed the interface of "
                     "'{}' without touched_interface",
                     pass_name, f->name.c_str());
        ASSERT(false);
      }
    }
  }
  before_module_pass.clear();
#else
  (void)pass_name;
#endif
  const bool any = !reported_bodies.empty() || !reported_interfaces.empty();
  reported_bodies.clear();
  reported_interfaces.clear();
  return reported && any;
}

bool AnalysisManager::verify_unchanged(const fir::Function &func,
                                       u64 hash_before, u64 hash_after,
                                       std::string_view pass_name) {
  if (hash_after == hash_before) {
    return true;
  }
  fmt::println(stderr,
               "AnalysisManager: pass '{}' returned PreservedAnalysis::all() "
               "for '{}' but changed it",
               pass_name, func.name.c_str());
  fmt::println("Pass returned wrong PreservedAnalysis (all)");
  return false;
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
