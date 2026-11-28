#include "optim/analysis/mem/global_escape.hpp"

#include <fmt/base.h>

#include "ir/global.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/mem/alloca_escape.hpp"
#include "utils/set.hpp"

namespace foptim::optim {

namespace {
bool is_private(fir::Context &ctx, fir::Global g,
                const TSet<fir::Global> &referenced_by_globals) {
  // a constant global is already read only, nothing to gain
  if (g->linkage != fir::Linkage::Internal || g->is_thread_local ||
      g->is_constant || g->has_alias || g->init_value == nullptr ||
      referenced_by_globals.contains(g)) {
    return false;
  }
  const fir::ValueR addr{ctx->get_constant_value(g)};
  // through_block_args pointer induction variables carry the address on.
  // calls_escape a callee of the module can access the global by name while
  // it reads through the parameter.
  const auto info = analyze_alloca_escape(
      addr, {.through_block_args = true, .calls_escape = true});
  // pointers stored into the global itself would have to be tracked
  return !info.escapes && !info.has_self_ptr;
}
}  // namespace

namespace {
void collect_globals(fir::Context &ctx, FVec<fir::Global> &globals,
                     TSet<fir::Global> &referenced_by_globals) {
  auto *slab = ctx->storage.storage_global._slot_start.load();
  while (slab != nullptr) {
    for (auto &slot : slab->data) {
      if (!slot.is_used()) {
        continue;
      }
#if SLOT_CHECK_LEVEL >= 2
      auto sref = utils::SRef<std::unique_ptr<fir::GlobalData>>{
          const_cast<utils::Slot<std::unique_ptr<fir::GlobalData>> *>(&slot),
          slot.generation()};
#else
      auto sref = utils::SRef<std::unique_ptr<fir::GlobalData>>{
          const_cast<utils::Slot<std::unique_ptr<fir::GlobalData>> *>(&slot),
          0};
#endif
      globals.emplace_back(std::move(sref));
      for (const auto &info : slot.data->reloc_info) {
        if (info.ref->is_global()) {
          referenced_by_globals.insert(info.ref->as_global());
        }
      }
    }
    slab = slab->next;
  }
}
}  // namespace

bool verify_private_globals(fir::Context &ctx) {
  TSet<fir::Global> referenced_by_globals;
  FVec<fir::Global> globals;
  collect_globals(ctx, globals, referenced_by_globals);
  for (auto g : globals) {
    if (g->addr_private && !is_private(ctx, g, referenced_by_globals)) {
      fmt::println(stderr,
                   "global '{}' was marked private but its address escapes now "
                   "(a pass gave it an escaping use)",
                   g->name.c_str());
      return false;
    }
  }
  return true;
}

void update_private_globals(fir::Context &ctx, AnalysisManager &analyMan) {
  TSet<fir::Global> referenced_by_globals;
  FVec<fir::Global> globals;
  collect_globals(ctx, globals, referenced_by_globals);
  for (auto g : globals) {
    const bool now = is_private(ctx, g, referenced_by_globals);
    if (g->addr_private == now) {
      continue;
    }
    g->addr_private = now;
    analyMan.touched_global(g);
    for (const auto &use : g->get_uses()) {
      AnalysisManager::drop_memory(*use.user->get_parent()->get_parent().func);
    }
  }
}

}  // namespace foptim::optim
