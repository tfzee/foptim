#pragma once
#include "ir/builder.hpp"
#include "ir/global.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "optim/module_pass.hpp"
#include "utils/set.hpp"
#include "utils/stable_vec_slot.hpp"

namespace foptim::optim {

class GlobalPromotion final : public ModulePass {
 public:
  void apply(fir::Context &ctx) override {
    // if we have a global thats linked internally
    //  and its only used in 1 function we can promote it to a local alloca
    TSet<fir::Global> global_global_reffed;

    {
      auto *slab = ctx->storage.storage_global._slot_start.load();
      while (slab != nullptr) {
        for (auto &i : slab->data) {
          const auto *v = &i;
          if (v->used.load() == foptim::utils::SlotState::Used) {
            for (auto info : v->data->reloc_info) {
              if (info.ref->is_global()) {
                global_global_reffed.insert(info.ref->as_global());
              }
            }
          }
        }
        slab = slab->next;
      }
    }

    {
      auto *slab = ctx->storage.storage_global._slot_start.load();
      while (slab != nullptr) {
        for (auto &i : slab->data) {
          const auto *v = &i;
          if (v->used.load() == foptim::utils::SlotState::Used) {
            //! CANCER!
            auto sref = utils::SRef<std::unique_ptr<fir::GlobalData>>{
                const_cast<utils::Slot<std::unique_ptr<fir::GlobalData>> *>(v),
                v->generation};
            auto global = fir::Global{std::move(sref)};
            switch (global->linkage) {
              case fir::Linkage::External:
              case fir::Linkage::Weak:
              case fir::Linkage::WeakODR:
              case fir::Linkage::LinkOnce:
              case fir::Linkage::LinkOnceODR:
                continue;
              case fir::Linkage::Internal:
                break;
            }
            // TODO: tweak
            if (global->n_bytes > 8) {
              continue;
            }
            if (global->is_constant) {
              continue;
            }
            if (global_global_reffed.contains(global)) {
              continue;
            }
            fir::Function *target_f = nullptr;
            TVec<fir::Use> uses{global->get_uses().begin(),
                                global->get_uses().end()};
            // TODO: would need a better check to check if it escapes / has
            // longer lifetime then this function
            for (auto u : uses) {
              auto funcy = u.user->get_parent()->get_parent();
              if (u.type == fir::UseType::NormalArg) {
                if ((u.user->is(fir::InstrType::StoreInstr) ||
                     u.user->is(fir::InstrType::LoadInstr)) &&
                    u.argId == 0) {
                } else {
                  target_f = nullptr;
                  break;
                }
              } else {
                target_f = nullptr;
                break;
              }
              if (target_f == nullptr || funcy == target_f) {
                target_f = funcy.func;
              } else {
                target_f = nullptr;
                break;
              }
            }
            if (target_f == nullptr || !target_f->no_recurse) {
              continue;
            }
            fir::Builder b{target_f};
            b.at_start(target_f->get_entry());

            auto new_val = b.build_alloca(
                fir::ValueR{ctx->get_constant_int(global->n_bytes, 32)});
            if (global->n_bytes % 8 == 0) {
              for (size_t i = 0; i < global->n_bytes; i += 8) {
                auto ptr = b.build_int_add(
                    new_val, fir::ValueR{ctx->get_constant_int(i, 64)});
                b.build_store(ptr,
                              fir::ValueR{ctx->get_constant_int(
                                  *((u64 *)(global->init_value + i)), 64)});
              }
            } else if (global->n_bytes % 4 == 0) {
              for (size_t i = 0; i < global->n_bytes; i += 4) {
                auto ptr = b.build_int_add(
                    new_val, fir::ValueR{ctx->get_constant_int(i, 64)});
                b.build_store(ptr,
                              fir::ValueR{ctx->get_constant_int(
                                  *((u32 *)(global->init_value + i)), 32)});
              }
            } else {
              for (size_t i = 0; i < global->n_bytes; i += 1) {
                auto ptr = b.build_int_add(
                    new_val, fir::ValueR{ctx->get_constant_int(i, 64)});
                b.build_store(ptr, fir::ValueR{ctx->get_constant_int(
                                       *((u8 *)(global->init_value + i)), 8)});
              }
            }
            for (auto use : uses) {
              use.replace_use(new_val);
            }
          }
        }
        slab = slab->next;
      }
    }
  }
};

}  // namespace foptim::optim
