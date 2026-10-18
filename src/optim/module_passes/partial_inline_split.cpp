#include "partial_inline_split.hpp"

#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/cfg.hpp"

namespace foptim::optim {

namespace {
constexpr size_t MaxTries = 67;

bool is_candidate(const fir::Function &f, const PartialInlineSplit::Config &c) {
  const auto &a = f.attribs;
  if (f.is_decl() || a.variadic || a.must_inline || a.no_inline || a.outlined ||
      a.cold || a.no_return) {
    return false;
  }
  switch (a.linkage) {
    case fir::Linkage::Weak:
    case fir::Linkage::LinkOnce:
      return false;
    case fir::Linkage::Internal:
    case fir::Linkage::External:
    case fir::Linkage::WeakODR:
    case fir::Linkage::LinkOnceODR:
      break;
  }
  if (f.n_instrs() < c.rest_min + c.prefix_max) {
    return false;
  }
  // a function with one caller is inlined as a whole anyway
  if (f.get_n_uses() == 0 ||
      (f.get_n_uses() == 1 && (a.linkage == fir::Linkage::Internal ||
                               a.linkage == fir::Linkage::LinkOnceODR))) {
    return false;
  }
  for (auto bb : f.basic_blocks) {
    for (auto instr : bb->instructions) {
      if (instr->is(fir::InstrType::AllocaInstr) &&
          !instr->args[0].is_constant()) {
        return false;
      }
    }
  }
  return true;
}

// entry block arguments the value is computed from with at most two steps
void collect_guard_args(fir::ValueR v, fir::BasicBlock entry, u32 depth,
                        TVec<u32> &out) {
  if (v.is_bb_arg()) {
    auto arg = v.as_bb_arg();
    if (arg->get_parent() == entry) {
      out.push_back(static_cast<u32>(entry->get_arg_id(arg)));
    }
  } else if (v.is_instr() && depth > 0) {
    for (auto a : v.as_instr()->args) {
      collect_guard_args(a, entry, depth - 1, out);
    }
  }
}

TVec<u32> guard_args(const CFG &cfg, u32 e, fir::BasicBlock entry) {
  TVec<u32> out;
  for (const u32 p : cfg.bbrs[e].pred) {
    auto term = cfg.bbrs[p].bb->get_terminator();
    if (term->is(fir::InstrType::CondBranchInstr) &&
        term->args[0].is_instr(fir::InstrType::ICmp)) {
      for (auto a : term->args[0].as_instr()->args) {
        collect_guard_args(a, entry, 2, out);
      }
    }
  }
  return out;
}

bool known_at_call(fir::ValueR v) {
  if (v.is_constant()) {
    return !v.is_poison();
  }
  return v.is_instr(fir::InstrType::AllocaInstr);
}

// some direct call passes a known value for a guarded argument, so the guard
// can fold once the wrapper is inlined there
bool guard_folds_somewhere(fir::Function &f, const TVec<u32> &args) {
  for (const auto &use : f.get_uses()) {
    if (use.type != fir::UseType::NormalArg || use.argId != 0 ||
        !use.user->is(fir::InstrType::CallInstr)) {
      continue;
    }
    for (const u32 a : args) {
      if (a + 1 < use.user->args.size() &&
          known_at_call(use.user->args[a + 1])) {
        return true;
      }
    }
  }
  return false;
}
}  // namespace

bool PartialInlineSplit::split(fir::Context &ctx, fir::Function &f,
                               AnalysisManager &analyMan) {
  CFG &cfg = AnalysisManager::cfg(f);
  const auto entry = f.get_entry();
  const size_t total = f.n_instrs();
  const RegionLimits limits{.min_instrs = config.rest_min,
                            .max_live_ins = config.max_live_ins};

  std::optional<Region> best;
  size_t tries = 0;
  for (u32 b = 0; b < cfg.bbrs.size() && tries < MaxTries; b++) {
    if (b == cfg.entry) {
      continue;
    }
    auto args = guard_args(cfg, b, entry);
    if (args.empty() || !guard_folds_somewhere(f, args)) {
      continue;
    }
    tries++;
    auto r = find_region(f, cfg.bbrs[b].bb, cfg, limits);
    if (!r || r->kind != Region::Kind::Returns ||
        total - r->n_instrs > config.prefix_max) {
      continue;
    }
    if (!best || r->n_instrs > best->n_instrs) {
      best = std::move(r);
    }
  }
  if (!best) {
    return false;
  }
  auto name = unique_function_name(*ctx.data, f.name + ".rest");
  extract_region(f, *best, name,
                 ExtractAttribs{.cold = false, .no_inline = true});
  auto *rest = ctx->storage.functions.get(name.c_str());
  analyMan.touched_body(f);
  analyMan.touched_body(*rest);
  analyMan.touched_interface(*rest);
  return true;
}

PreservedAnalysis PartialInlineSplit::apply(fir::Context &ctx,
                                            JobSheduler * /*shed*/,
                                            AnalysisManager &analyMan) {
  ZoneScopedNC("PartialInlineSplit", COLOR_OPTIMF);
  const auto funcs = ctx->storage.functions.all();
  TVec<fir::Function *> work(funcs.begin(), funcs.end());
  for (auto *f : work) {
    if (is_candidate(*f, config)) {
      split(ctx, *f, analyMan);
    }
  }
  return PreservedAnalysis::all();
}

}  // namespace foptim::optim
