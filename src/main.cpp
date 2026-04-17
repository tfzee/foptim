#include <fmt/base.h>
#include <fmt/core.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>

#include "arg_parsing/parser.hpp"
#include "ir/context.hpp"
#include "ir/function_ref.hpp"
#include "ir/helpers.hpp"
#include "llvm/llir_loader.hpp"
#include "mir/func.hpp"
#include "mir/legalize_bb_form.hpp"
#include "mir/matcher.hpp"
#include "mir/optim/bb_reordering.hpp"
#include "mir/optim/calling_conv.hpp"
#include "mir/optim/copy_prop.hpp"
#include "mir/optim/dce.hpp"
#include "mir/optim/inst_simplify.hpp"
#include "mir/optim/legalization.hpp"
#include "mir/optim/lifetime_shortening.hpp"
#include "mir/optim/lvn.hpp"
#include "mir/optim/reg_alloc.hpp"
#include "mir/optim/register_joining.hpp"
#include "mir/optim/stack_optim.hpp"
#include "optim/func_passes/cmp_known_val_prop.hpp"
#include "optim/func_passes/constant_loop_eval.hpp"
#include "optim/func_passes/dce.hpp"
#include "optim/func_passes/double_load.hpp"
#include "optim/func_passes/inst_simplify.hpp"
#include "optim/func_passes/intrin_simplify.hpp"
#include "optim/func_passes/legalize_struct.hpp"
#include "optim/func_passes/legalize_vecs.hpp"
#include "optim/func_passes/licm.hpp"
#include "optim/func_passes/llvm_intrin_lowering.hpp"
#include "optim/func_passes/loop_rotate.hpp"
#include "optim/func_passes/loop_simplify.hpp"
#include "optim/func_passes/loop_unroll.hpp"
#include "optim/func_passes/loop_unswitch.hpp"
#include "optim/func_passes/lvn.hpp"
#include "optim/func_passes/mem2reg.hpp"
#include "optim/func_passes/merge_alloca.hpp"
#include "optim/func_passes/sccp.hpp"
#include "optim/func_passes/simplify_cfg.hpp"
#include "optim/func_passes/slp_vectorizer.hpp"
#include "optim/func_passes/sora.hpp"
#include "optim/func_passes/stack_known_bits.hpp"
#include "optim/func_passes/tail_rec_elim.hpp"
#include "optim/function_pass.hpp"
#include "optim/module_passes/IPCP.hpp"
#include "optim/module_passes/arg_promotion.hpp"
#include "optim/module_passes/func_property_annotator.hpp"
#include "optim/module_passes/function_dedup.hpp"
#include "optim/module_passes/global_dce.hpp"
#include "optim/module_passes/global_promotion.hpp"
#include "optim/module_passes/inline.hpp"
#include "utils/arena.hpp"
#include "utils/job_system.hpp"
#include "utils/parameters.hpp"
#include "utils/stable_vec_ref.hpp"
#include "utils/stable_vec_slot.hpp"
#include "utils/stats.hpp"
#include "utils/timer.hpp"
#include "utils/todo.hpp"
#include "utils/tracy.hpp"
#include "x86_codegen/backend.hpp"

namespace {
void parse_llvm_ir(foptim::fir::Context &ctx, foptim::JobSheduler &shed);
void optimize_fir(foptim::fir::Context &ctx, foptim::JobSheduler *shed);
void lower_to_mir_and_optimize(foptim::fir::Context &ctx,
                               foptim::FVec<foptim::fmir::MFunc> &funcs,
                               foptim::FVec<foptim::fmir::Global> &globals,
                               foptim::JobSheduler *shed);
void codegen(foptim::FVec<foptim::fmir::MFunc> &funcs,
             foptim::FVec<foptim::IRString> &decls,
             foptim::FVec<foptim::fmir::Global> &globals);

}  // namespace

int main(int argc, char *argv[]) {
  ZoneScopedN("BASE");
  parse_args(argc, argv);
  foptim::utils::DumbTimer t;
  foptim::JobSheduler shed;
  shed.init(foptim::utils::number_worker_threads);
  if (foptim::utils::verbosity > 0) {
    fmt::println("Running with {} Workers",
                 foptim::utils::number_worker_threads);
  }

  {
    auto a1 = t.scopedTimer("CompileTime");

    foptim::fir::Context ctx;
    {
      // fir
      {
        auto a1 = t.scopedTimer("ParseConvert");
        parse_llvm_ir(ctx, shed);
      }
      {
        auto a1 = t.scopedTimer("Optimize");
        optimize_fir(ctx, &shed);
      }
    }

    // mir
    foptim::FVec<foptim::fmir::MFunc> funcs;
    foptim::FVec<foptim::fmir::Global> globals;
    foptim::FVec<foptim::IRString> decls;
    {
      auto a1 = t.scopedTimer("Mir");
      for (auto &[decl, f] : ctx.data->storage.functions) {
        if (f->is_decl()) {
          decls.push_back(decl);
        }
      }
      lower_to_mir_and_optimize(ctx, funcs, globals, &shed);
    }
    {
      auto a1 = t.scopedTimer("Codegen");
      // asm
      codegen(funcs, decls, globals);
    }

    foptim::utils::StatCollector::get().dump();
    shed.wait_till_done();
    ctx.free();
    shed.deinit();
  }
  t.print();
  // cleanup
  foptim::utils::TempAlloc<void *>::free();
  foptim::utils::IRAlloc<void *>::free();
  return 0;
}

namespace {
void parse_llvm_ir(foptim::fir::Context &ctx, foptim::JobSheduler &shed) {
  ZoneScopedN("LLIR LOADING");
  load_llvm_ir(foptim::utils::in_file_path.c_str(), ctx, shed);
  foptim::utils::TempAlloc<void *>::reset();
}

void optimize_fir(foptim::fir::Context &ctx, foptim::JobSheduler *shed) {
  (void)shed;
  ZoneScopedN("Optim FIR");
  using namespace foptim::optim;
  if (foptim::utils::verbosity > 0) {
    fmt::print("================FIR====================\n");
    fmt::print("================FIR START====================\n");
  }
  ASSERT(ctx->verify());
  foptim::optim::StaticParallelFunctionPassManager<DCE>{}.apply(ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<
      LegalizeStructs, LLVMInstrinsicLowering, SORA, Mem2Reg, DoubleLoadElim,
      DCE, IntrinSimplify, InstSimplify, DCE, SimplifyCFG, LVN, DCE>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<FuncPropAnnotator, GlobalPromotion,
                                         ArgPromotion, GDCE>{}
      .apply(ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<
      DCE, CmpKnownValProp, SimplifyCFG, TailRecElim, LICM, LoopRotate,
      LoopSimplify, DCE, LVN, SCCP, IntrinSimplify, InstSimplify,
      DoubleLoadElim, DCE, SimplifyCFG, StackKnownBits, SORA, Mem2Reg,
      SimplifyCFG, DCE, LVN, InstSimplify, ConstLoopEval, LoopSimplify,
      InstSimplify, SimplifyCFG>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<
      FuncPropAnnotator, IPCP, GlobalPromotion, Inline<>, Inline<>, Inline<>,
      ArgPromotion, GDCE, FunctionDeDup<true>, GDCE, FuncPropAnnotator>{}
      .apply(ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<
      SimplifyCFG, CmpKnownValProp, InstSimplify, SimplifyCFG, LICM, DCE,
      LoopSimplify, LoopUnswitch, LoopUnroll, SimplifyCFG, DCE, SLPVectorizer,
      LVN, SCCP, IntrinSimplify, InstSimplify, ConstLoopEval, InstSimplify,
      CmpKnownValProp, SimplifyCFG, DCE>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<
      FuncPropAnnotator, IPCP, GlobalPromotion, Inline<>, Inline<>, Inline<>,
      ArgPromotion, FunctionDeDup<true>, GDCE, FuncPropAnnotator>{}
      .apply(ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<
      SimplifyCFG, InstSimplify, SimplifyCFG, TailRecElim, SimplifyCFG, DCE,
      LoopSimplify, LoopUnswitch, IntrinSimplify, InstSimplify, DCE>{}
      .apply(ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<StackKnownBits, SORA,
                                                   Mem2Reg, SimplifyCFG, DCE>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<
      ArgPromotion, FuncPropAnnotator, FunctionDeDup<false>, GDCE, IPCP,
      GlobalPromotion, Inline<>, Inline<>, GDCE, FuncPropAnnotator>{}
      .apply(ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<
      LVN, SCCP, DoubleLoadElim, DCE, IntrinSimplify, InstSimplify,
      CmpKnownValProp, SimplifyCFG, SCCP>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<
      ArgPromotion, FuncPropAnnotator, FunctionDeDup<false>, Inline<>, Inline<>,
      GDCE, IPCP, FuncPropAnnotator>{}
      .apply(ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<
      SimplifyCFG, LVN, SCCP, DoubleLoadElim, DCE, IntrinSimplify, SimplifyCFG,
      InstSimplify, SCCP, DCE, InstSimplify, ConstLoopEval, LoopSimplify,
      LoopUnswitch, LoopUnroll, SimplifyCFG, DCE, SLPVectorizer, InstSimplify,
      SimplifyCFG>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<
      ArgPromotion, FuncPropAnnotator, FunctionDeDup<false>, Inline<>, Inline<>,
      GDCE, IPCP, FuncPropAnnotator>{}
      .apply(ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<
      SimplifyCFG, LVN, SCCP, DCE, IntrinSimplify, InstSimplify, DCE,
      SLPVectorizer, InstSimplify, SimplifyCFG>{}
      .apply(ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<
      LegalizeVecs, SCCP, LVN, InstSimplify, DCE, LVN, InstSimplify, DCE>{}
      .apply(ctx, shed);
  for (const auto &[_, func] : ctx->storage.functions) {
    fmt::println("{:cd}", *func);
  }
  // // general cleanup / legalization / finalization
  foptim::optim::StaticParallelFunctionPassManager<MergeAllocaPass>{}.apply(
      ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<
      LVN, InstSimplify, SCCP, DCE, LVN, InstSimplify, SimplifyCFG, DCE,
      LegalizeVecs, InstSimplify, SCCP, LegalizeVecs, DCE>{}
      .apply(ctx, shed);

  ASSERT(ctx->verify());
  // {
  //   auto *slab = ctx->storage.storage_global._slot_start.load();
  //   while (slab != nullptr) {
  //     for (auto &i : slab->data) {
  //       const auto *v = &i;
  //       if (v->used == foptim::utils::SlotState::Used) {
  //         // auto size = v->data->n_bytes;
  //         // foptim::fmir::Global glob = {.name = v->data->name.c_str(),
  //         //                              .data = {},
  //         //                              .size = 0,
  //         //                              .reloc_info = {},
  //         //                              .vis = v->data->linkvis};
  //         fmt::println("{:cd}", *v->data);
  //       }
  //     }
  //     slab = slab->next;
  //   }
  // }
  if (foptim::utils::verbosity > 0) {
    fmt::print("================FIR END====================\n");
  }
}

void reorder_funcs(foptim::TVec<foptim::fir::Function *> &reordered_funcs) {
  (void)reordered_funcs;
  std::ranges::sort(reordered_funcs,
                    [](foptim::fir::Function *a, foptim::fir::Function *b) {
                      return a->get_n_uses() < b->get_n_uses();
                    });
}

void lower_to_mir_and_optimize(foptim::fir::Context &ctx,
                               foptim::FVec<foptim::fmir::MFunc> &funcs,
                               foptim::FVec<foptim::fmir::Global> &globals,
                               foptim::JobSheduler *shed) {
  ZoneScopedN("MIR stuff");
  funcs.reserve(ctx->storage.functions.size());
  globals.reserve(ctx->storage.storage_global.n_used());

  {
    auto *slab = ctx->storage.storage_global._slot_start.load();
    while (slab != nullptr) {
      for (auto &i : slab->data) {
        const auto *v = &i;
        if (v->used == foptim::utils::SlotState::Used) {
          auto size = v->data->n_bytes;
          foptim::fmir::Global glob = {.name = v->data->name.c_str(),
                                       .data = {},
                                       .size = 0,
                                       .reloc_info = {},
                                       .vis = v->data->linkvis};
          for (const auto &rel_inf : v->data->reloc_info) {
            if (rel_inf.ref->is_global()) {
              glob.reloc_info.push_back(foptim::fmir::Global::RelocationInfo{
                  .insert_offset = rel_inf.insert_offset,
                  .name = rel_inf.ref->as_global()->name.c_str(),
                  .reloc_offset = rel_inf.reloc_offset,
              });
            } else if (rel_inf.ref->is_func()) {
              glob.reloc_info.push_back(foptim::fmir::Global::RelocationInfo{
                  .insert_offset = rel_inf.insert_offset,
                  .name = rel_inf.ref->as_func()->name.c_str(),
                  .reloc_offset = rel_inf.reloc_offset,
              });
            } else {
              TODO("dont think theeres any others");
            }
          }
          glob.size = size;
          if (v->data->init_value != nullptr) {
            glob.data.resize(size, 0);
            memcpy(glob.data.data(), v->data->init_value, size);
          }
          globals.push_back(glob);
        }
      }
      slab = slab->next;
    }
  }

  foptim::TVec<foptim::fir::Function *> reordered_funcs;
  reordered_funcs.reserve(ctx->storage.functions.size());
  for (auto &[_, func] : ctx->storage.functions) {
    reordered_funcs.emplace_back(func.get());
  }
  reorder_funcs(reordered_funcs);
  if (foptim::utils::verbosity > 0) {
    fmt::print("================MATCHING====================\n");
    fmt::println(" Got {} functions", reordered_funcs.size());
    ctx.data->print_stats();
  }

  size_t n_reordered_def_funcs = 0;
  for (auto *func : reordered_funcs) {
    if (func->is_decl()) {
      continue;
    }
    n_reordered_def_funcs += 1;
  }
  funcs.resize(n_reordered_def_funcs);
  size_t i = 0;
  for (auto *reord_func : reordered_funcs) {
    if (reord_func->is_decl()) {
      continue;
    }
    shed->push(nullptr, [i, &funcs, reord_func]() {
      auto &func = funcs.at(i);
      auto matcher = foptim::fmir::GreedyMatcher{};
      func = matcher.apply(*reord_func);
      ASSERT(foptim::fmir::verify(func));
      foptim::fmir::LegalizeBBForm{}.apply(func);
      foptim::fmir::DeadCodeElim{}.apply(func);
      foptim::fmir::CopyPropagation{}.apply(func);
      foptim::fmir::DeadCodeElim{}.apply(func);
      foptim::fmir::BBReordering{}.apply(func);
      foptim::fmir::DeadCodeElim{}.apply(func);
      foptim::fmir::LVN{}.apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::CopyPropagation{}.apply(func);
      foptim::fmir::InstSimplify{}.early_apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::LifetimeShortening{}.apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::CopyPropagation{}.apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::InstSimplify{}.early_apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::DeadCodeElim{}.apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      ASSERT(foptim::fmir::verify(func));
      foptim::fmir::CallingConv{}.first_stage(func);
      foptim::fmir::Legalizer{}.apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::InstSimplify{}.early_apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::RegisterJoining{}.apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::InstSimplify{}.early_apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::RegAlloc{}.apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::CallingConv{}.second_stage(func);
      foptim::fmir::StackOptim{}.apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::InstSimplify{}.apply(func);
      foptim::fmir::InstSimplify{}.apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::BBReordering{}.apply(func);
      foptim::utils::TempAlloc<void *>::reset();
      ASSERT(foptim::fmir::verify(func));
    });
    i++;
  }
  shed->wait_till_done();
  // for (auto &f : funcs) {
  //   fmt::println("{:cd}", f);
  // }
}

void codegen(foptim::FVec<foptim::fmir::MFunc> &funcs,
             foptim::FVec<foptim::IRString> &decls,
             foptim::FVec<foptim::fmir::Global> &globals) {
  ZoneScopedN("Codegen");
  foptim::codegen::run(funcs, decls, globals);
  fmt::println("Done!");
}
}  // namespace
