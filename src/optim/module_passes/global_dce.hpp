#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/module_pass.hpp"
#include "utils/arena.hpp"
#include "utils/logging.hpp"
#include "utils/set.hpp"
#include <algorithm>

namespace foptim::optim {

class GDCE final : public ModulePass {
public:
  bool is_intrinsic(const IRString &name) {
    return name.starts_with("llvm.") || name.starts_with("foptim.");
  }

  void apply(fir::Context &ctx) override {
    ZoneScopedN("GDCE");

    // NOTE: since uses do not cover globals using other globals yet
    // we need to figure out if a functoin/global might be reffed by another
    std::unordered_set<const fir::Function *> func_global_reffed;
    for (const auto *slab_g : ctx->storage.storage_global._slot_slab_starts) {
      for (size_t i = 0;
           i < decltype(ctx->storage.storage_global)::_slot_slab_len; i++) {
        const auto *v = &slab_g[i];
        if (v->used == foptim::utils::SlotState::Used) {
          for (auto info : v->data->reloc_info) {
            if (info.ref->is_float()) {
              func_global_reffed.insert(info.ref->as_func().func);
            }
          }
        }
      }
    }

    // TODO: dead global variable deletion
    for (auto &[name, f] : ctx.data->storage.functions) {
      if (f->get_n_uses() > 0) {
        continue;
      }
      if (is_intrinsic(name)) {
        ctx.data->storage.functions.erase(name);
        continue;
      }
      switch (f->linkage) {
      case fir::Function::Linkage::External:
      case fir::Function::Linkage::WeakODR:
      case fir::Function::Linkage::Weak:
        continue;
      case fir::Function::Linkage::Internal:
      case fir::Function::Linkage::LinkOnce:
      case fir::Function::Linkage::LinkOnceODR:
        break;
      }
      if (func_global_reffed.contains(f.get())) {
        continue;
      }
      if (name.starts_with("_GLOBAL")) {
        continue;
      }

      // TODO: need to figure out if its reffed by a global object
      ctx.data->storage.functions.erase(name);
    }
  }
};
} // namespace foptim::optim
