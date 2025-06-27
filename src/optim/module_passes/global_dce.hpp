#pragma once
#include "ir/function.hpp"
#include "ir/global.hpp"
#include "ir/instruction_data.hpp"
#include "optim/module_pass.hpp"
#include "utils/set.hpp"

namespace foptim::optim {

class GDCE final : public ModulePass {
public:
  bool is_intrinsic(const IRString &name) {
    return name.starts_with("llvm.") || name.starts_with("foptim.");
  }

  void apply(fir::Context &ctx) override {
    ZoneScopedN("GDCE");

    TSet<const fir::Function *> func_global_reffed;
    TSet<fir::Global> global_global_reffed;
    for (const auto *slab_g : ctx->storage.storage_global._slot_slab_starts) {
      for (size_t i = 0;
           i < decltype(ctx->storage.storage_global)::_slot_slab_len; i++) {
        const auto *v = &slab_g[i];
        if (v->used == foptim::utils::SlotState::Used) {
          for (auto info : v->data->reloc_info) {
            if (info.ref->is_func()) {
              func_global_reffed.insert(info.ref->as_func().func);
            }
            if (info.ref->is_global()) {
              global_global_reffed.insert(info.ref->as_global());
            }
          }
        }
      }
    }

    for (auto *slab_g : ctx->storage.storage_global._slot_slab_starts) {
      for (size_t i = 0;
           i < decltype(ctx->storage.storage_global)::_slot_slab_len; i++) {
        auto *v = &slab_g[i];
        if (v->used == foptim::utils::SlotState::Used) {
          auto g =
              fir::Global{utils::SRef<std::unique_ptr<foptim::fir::GlobalData>>{
                  v,
#ifdef SLOT_CHECK_GENERATION
                  v->generation
#else
                  0
#endif
              }};
          if (g->get_n_uses() > 0) {
            continue;
          }
          switch (g->linkage) {
          case fir::Linkage::External:
          case fir::Linkage::WeakODR:
          case fir::Linkage::Weak:
            continue;
          case fir::Linkage::Internal:
          case fir::Linkage::LinkOnce:
          case fir::Linkage::LinkOnceODR:
            break;
          }
          if (global_global_reffed.contains(g)) {
            continue;
          }
          // if (name.starts_with("_GLOBAL")) {
          //   continue;
          // }

          ctx->storage.storage_global.remove(g);
        }
      }
    }

    for (auto &[name, f] : ctx.data->storage.functions) {
      if (f->get_n_uses() > 0) {
        continue;
      }
      if (func_global_reffed.contains(f.get())) {
        continue;
      }
      if (is_intrinsic(name)) {
        // can always delete these even if they are external
        ctx.data->storage.functions.erase(name);
        continue;
      }
      switch (f->linkage) {
      case fir::Linkage::External:
      case fir::Linkage::WeakODR:
      case fir::Linkage::Weak:
        continue;
      case fir::Linkage::Internal:
      case fir::Linkage::LinkOnce:
      case fir::Linkage::LinkOnceODR:
        break;
      }
      if (name.starts_with("_GLOBAL")) {
        continue;
      }

      while (!f->basic_blocks.empty()) {
        f->basic_blocks.back()->remove_from_parent(true, true, true);
      }
      ctx.data->storage.functions.erase(name);
    }
  }
};
} // namespace foptim::optim
