#pragma once
#include "ir/builder.hpp"
#include "ir/global.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/module_pass.hpp"
#include "utils/set.hpp"
#include "utils/stable_vec_slot.hpp"
#include "utils/tracy.hpp"
#include <cstring>
#include <fmt/base.h>

namespace foptim::optim {

class GlobalPromotion final : public ModulePass {
public:
  PreservedAnalysis apply(fir::Context &ctx, JobSheduler * /*shed*/,
                          AnalysisManager &analyMan) override {
    ZoneScopedNC("GlobalPromotion", COLOR_OPTIMF);
    // if we have a global thats linked internally
    //  and its only used in 1 function we can promote it to a local alloca
    TSet<fir::Global> global_global_reffed;

    {
      auto *slab = ctx->storage.storage_global._slot_start.load();
      while (slab != nullptr) {
        for (auto &i : slab->data) {
          const auto *v = &i;
          if (v->is_used()) {
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

    const auto is_written = [](fir::Global x) -> bool {
      TVec<fir::Use> uses;
      uses.reserve(x->get_n_uses());
      for (auto use : x->get_uses()) {
        uses.push_back(use);
      }
      while (!uses.empty()) {
        fir::Use c = uses.back();
        uses.pop_back();
        // TODO expand ot more shit i guess
        if (c.user->is(fir::BinaryInstrSubType::PtrAdd) ||
            c.user->is(fir::InstrType::SelectInstr)) {
          for (auto u : c.user->get_uses()) {
            uses.push_back(u);
          }
        } else if (!c.user->is(fir::InstrType::LoadInstr)) {
          return true;
        }
      }
      return false;
    };

    {
      auto *slab = ctx->storage.storage_global._slot_start.load();
      while (slab != nullptr) {
        for (auto &i : slab->data) {
          const auto *v = &i;
          if (v->is_used()) {
            //! CANCER!
#if SLOT_CHECK_LEVEL >= 2
            auto sref = utils::SRef<std::unique_ptr<fir::GlobalData>>{
                const_cast<utils::Slot<std::unique_ptr<fir::GlobalData>> *>(v),
                v->generation()};
#else
            auto sref = utils::SRef<std::unique_ptr<fir::GlobalData>>{
                const_cast<utils::Slot<std::unique_ptr<fir::GlobalData>> *>(v),
                0};
#endif
            auto global = fir::Global{std::move(sref)};
            switch (global->linkage) {
            case fir::Linkage::External:
            case fir::Linkage::Weak:
            case fir::Linkage::WeakODR:
            case fir::Linkage::LinkOnce:
              continue;
            case fir::Linkage::LinkOnceODR:
            case fir::Linkage::Internal:
              break;
            }

            if (global->n_bytes >= 8) {
              continue;
            }
            if (global_global_reffed.contains(global)) {
              continue;
            }
            if (!global->is_constant) {
              // if internal and no writes and no global_global_reffed just mark
              // it as constant
              if (global->linkage == fir::Linkage::Internal &&
                  !is_written(global)) {
                global->is_constant = true;
                analyMan.touched_global(global);
              } else {
                continue;
              }
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
                    u.argId == 0 && !u.user->Atomic && !u.user->Volatile) {
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
            // the alloca is fresh on every call, so the global's value would
            // not persist between calls: only valid if the function runs at
            // most once, which we only know for main
            if (target_f == nullptr || !target_f->attribs.no_recurse ||
                std::strcmp(target_f->getName().c_str(), "main") != 0) {
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
                b.build_store(
                    ptr,
                    fir::ValueR{ctx->get_constant_int(
                        *(reinterpret_cast<u64 *>(global->init_value + i)),
                        64)},
                    false, false);
              }
            } else if (global->n_bytes % 4 == 0) {
              for (size_t i = 0; i < global->n_bytes; i += 4) {
                auto ptr = b.build_int_add(
                    new_val, fir::ValueR{ctx->get_constant_int(i, 64)});
                b.build_store(
                    ptr,
                    fir::ValueR{ctx->get_constant_int(
                        *(reinterpret_cast<u32 *>(global->init_value + i)),
                        32)},
                    false, false);
              }
            } else {
              for (size_t i = 0; i < global->n_bytes; i += 1) {
                auto ptr = b.build_int_add(
                    new_val, fir::ValueR{ctx->get_constant_int(i, 64)});
                b.build_store(
                    ptr,
                    fir::ValueR{ctx->get_constant_int(
                        *(static_cast<u8 *>(global->init_value + i)), 8)},
                    false, false);
              }
            }
            for (auto use : uses) {
              use.replace_use(new_val);
            }
            analyMan.touched_body(*target_f);
          }
        }
        slab = slab->next;
      }
    }
    return PreservedAnalysis::all();
  }
};

} // namespace foptim::optim
