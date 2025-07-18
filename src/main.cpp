#include "arg_parsing/parser.hpp"
#include "ir/context.hpp"
#include "ir/function_ref.hpp"
#include "ir/helpers.hpp"
#include "mir/func.hpp"
#include "mir/legalize_bb_form.hpp"
#include "mir/matcher.hpp"
#include "mir/optim/bb_reordering.hpp"
#include "mir/optim/calling_conv.hpp"
#include "mir/optim/dce.hpp"
#include "mir/optim/inst_simplify.hpp"
#include "mir/optim/legalization.hpp"
#include "mir/optim/lifetime_shortening.hpp"
#include "mir/optim/reg_alloc.hpp"
#include "mir/optim/register_joining.hpp"
#include "optim/func_passes/constant_loop_eval.hpp"
#include "optim/func_passes/dce.hpp"
#include "optim/func_passes/func_annotator.hpp"
#include "optim/func_passes/garbage_collector.hpp"
#include "optim/func_passes/inst_simplify.hpp"
#include "optim/func_passes/intrin_simplify.hpp"
#include "optim/func_passes/legalize_struct.hpp"
#include "optim/func_passes/legalize_vecs.hpp"
#include "optim/func_passes/licm.hpp"
#include "optim/func_passes/llvm_intrin_lowering.hpp"
#include "optim/func_passes/loop_rotate.hpp"
#include "optim/func_passes/loop_simplify.hpp"
#include "optim/func_passes/loop_unroll.hpp"
#include "optim/func_passes/lvn.hpp"
#include "optim/func_passes/mem2reg.hpp"
#include "optim/func_passes/merge_alloca.hpp"
#include "optim/func_passes/sccp.hpp"
#include "optim/func_passes/simplify_cfg.hpp"
#include "optim/func_passes/slp_vectorizer.hpp"
#include "optim/func_passes/stack_known_bits.hpp"
#include "optim/func_passes/tail_rec_elim.hpp"
#include "optim/function_pass.hpp"
#include "optim/module_passes/IPCP.hpp"
#include "optim/module_passes/arg_promotion.hpp"
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
#include "x86_codegen/backend.hpp"
#include "llvm/llir_loader.hpp"

#include <algorithm>
#include <fmt/core.h>
#include <tracy/Tracy.hpp>
#include <unistd.h>

namespace {
void parse_llvm_ir(foptim::fir::Context &ctx);
void optimize_fir(foptim::fir::Context &ctx, foptim::JobSheduler *shed);
void lower_to_mir(foptim::fir::Context &ctx,
                  foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals);
void optimize_mir(foptim::fir::Context &ctx,
                  foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals);
void codegen(foptim::FVec<foptim::fmir::MFunc> &funcs,
             foptim::FVec<foptim::IRString> &decls,
             foptim::FVec<foptim::fmir::Global> &globals);

foptim::JobSheduler shed;

} // namespace

int main(int argc, char *argv[]) {
  ZoneScopedN("BASE");
  foptim::utils::DumbTimer t;

  {
    auto a1 = t.scopedTimer("CompileTime");
    parse_args(argc, argv);

    foptim::fir::Context ctx;
    {
      foptim::JobSheduler shed;
      shed.init(foptim::utils::number_worker_threads);
      // fir
      {
        auto a1 = t.scopedTimer("ParseConvert");
        parse_llvm_ir(ctx);
      }
      {
        auto a1 = t.scopedTimer("Optimize");
        optimize_fir(ctx, &shed);
      }
      // cleanup
      shed.deinit();
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
      lower_to_mir(ctx, funcs, globals);
      optimize_mir(ctx, funcs, globals);
    }
    {
      auto a1 = t.scopedTimer("Codegen");
      // asm
      codegen(funcs, decls, globals);
    }

    foptim::utils::StatCollector::get().dump();
    ctx.free();
  }
  t.print();
  foptim::utils::TempAlloc<void *>::free();
  foptim::utils::IRAlloc<void *>::free();
  return 0;
}

namespace {
void parse_llvm_ir(foptim::fir::Context &ctx) {
  ZoneScopedN("LLIR LOADING");
  load_llvm_ir(foptim::utils::in_file_path.c_str(), ctx);
  foptim::utils::TempAlloc<void *>::reset();
}

void optimize_fir(foptim::fir::Context &ctx, foptim::JobSheduler *shed) {
  (void)shed;
  ZoneScopedN("Optim FIR");
  using namespace foptim::optim;
  fmt::print("================FIR====================\n");
  fmt::print("================FIR START====================\n");
  foptim::optim::StaticParallelFunctionPassManager<
      LegalizeStructs, Mem2Reg, FuncAnnotator, InstSimplify, SimplifyCFG,
      LLVMInstrinsicLowering, DCE, GarbageCollect>{}
      .apply(ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<
      DCE, SimplifyCFG, TailRecElim, LICM, LoopRotate, LoopSimplify, DCE,
      SLPVectorizer, LVN, SCCP, InstSimplify, DCE, FuncAnnotator, SimplifyCFG,
      StackKnownBits, Mem2Reg, SimplifyCFG, DCE, InstSimplify, ConstLoopEval,
      LoopSimplify, InstSimplify, SimplifyCFG, GarbageCollect>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<IPCP, GlobalPromotion, Inline<>,
                                         Inline<>, ArgPromotion,
                                         FunctionDeDup<true>, GDCE>{}
      .apply(ctx);
  foptim::optim::StaticParallelFunctionPassManager<
      InstSimplify, SimplifyCFG, LICM, DCE, FuncAnnotator, LoopSimplify,
      LoopUnroll, SLPVectorizer, GarbageCollect, LVN, SCCP, IntrinSimplify,
      InstSimplify, ConstLoopEval, InstSimplify, SimplifyCFG, DCE,
      GarbageCollect>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<IPCP, GlobalPromotion, Inline<>,
                                         Inline<>, ArgPromotion,
                                         FunctionDeDup<true>, GDCE>{}
      .apply(ctx);
  foptim::optim::StaticParallelFunctionPassManager<
      InstSimplify, SimplifyCFG, TailRecElim, SimplifyCFG, DCE, LoopSimplify,
      FuncAnnotator, IntrinSimplify, InstSimplify, DCE, GarbageCollect>{}
      .apply(ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<StackKnownBits, Mem2Reg, DCE,
                                                   FuncAnnotator>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<FunctionDeDup<false>, GDCE, IPCP,
                                         GlobalPromotion, Inline<>, GDCE>{}
      .apply(ctx);
  foptim::optim::StaticParallelFunctionPassManager<
      LVN, SCCP, DCE, GarbageCollect, IntrinSimplify, SimplifyCFG, InstSimplify,
      SCCP, DCE, FuncAnnotator, InstSimplify, ConstLoopEval, LoopSimplify,
      LoopUnroll, SLPVectorizer, InstSimplify, SimplifyCFG, LegalizeVecs, DCE,
      GarbageCollect>{}
      .apply(ctx, shed);

  fmt::print("{:d}", ctx);
  // general cleanup / legalization / finalization
  foptim::optim::StaticParallelFunctionPassManager<MergeAllocaPass>{}.apply(
      ctx, shed);
  foptim::optim::StaticParallelFunctionPassManager<
      InstSimplify, LVN, SCCP, SimplifyCFG, DCE, GarbageCollect, LegalizeVecs>{}
      .apply(ctx, shed);
  foptim::optim::StaticModulePassManager<FunctionDeDup<false>, GDCE>{}.apply(
      ctx);
  ASSERT(ctx->verify());
  fmt::print("================FIR END====================\n");
}

void reorder_funcs(foptim::TVec<foptim::fir::Function *> &reordered_funcs) {
  (void)reordered_funcs;
  std::ranges::sort(reordered_funcs,
                    [](foptim::fir::Function *a, foptim::fir::Function *b) {
                      return a->get_n_uses() < b->get_n_uses();
                    });
}

void lower_to_mir(foptim::fir::Context &ctx,
                  foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals) {
  ZoneScopedN("FIR to MIR lowering");
  funcs.reserve(ctx->storage.functions.size());
  globals.reserve(ctx->storage.storage_global.n_used());

  for (const auto *slab_g : ctx->storage.storage_global._slot_slab_starts) {
    for (size_t i = 0;
         i < decltype(ctx->storage.storage_global)::_slot_slab_len; i++) {
      const auto *v = &slab_g[i];
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
  }

  auto matcher = foptim::fmir::GreedyMatcher{};
  foptim::TVec<foptim::fir::Function *> reordered_funcs;
  reordered_funcs.reserve(ctx->storage.functions.size());
  for (auto &[_, func] : ctx->storage.functions) {
    reordered_funcs.emplace_back(func.get());
  }
  reorder_funcs(reordered_funcs);
  fmt::print("================MATCHING====================\n");
  fmt::println(" Got {} functions", reordered_funcs.size());
  ctx.data->print_stats();
  for (auto *func : reordered_funcs) {
    // fmt::println("{:d}", *func);
    auto mark = foptim::utils::TempAlloc<void *>::save();

    if (func->is_decl()) {
      continue;
    }
    auto res = matcher.apply(*func);
    funcs.push_back(std::move(res));
    foptim::utils::TempAlloc<void *>::restore(mark);
  }
}

void optimize_mir(foptim::fir::Context &ctx,
                  foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::FVec<foptim::fmir::Global> &globals) {
  (void)globals;
  fmt::print("================MIR START====================\n");
  ZoneScopedN("MIR Optim");
  // running dead to make inst simplify work better
  foptim::fmir::LegalizeBBForm{}.apply(funcs);
  foptim::fmir::DeadCodeElim{}.apply(funcs);
  ASSERT(foptim::fmir::verify(funcs));
  foptim::utils::TempAlloc<void *>::reset();
  ctx.data->storage.storage_instr.collect_garbage();
  foptim::fmir::InstSimplify{}.early_apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::LifetimeShortening{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::InstSimplify{}.early_apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::DeadCodeElim{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  ctx.data->storage.storage_instr.collect_garbage();
  foptim::fmir::CallingConv{}.first_stage(funcs);
  foptim::fmir::Legalizer{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::InstSimplify{}.early_apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::RegisterJoining{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::InstSimplify{}.early_apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  // foptim::fmir::RegAllocWP{}.apply(funcs);
  // foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::RegAlloc{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::CallingConv{}.second_stage(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::InstSimplify{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
  foptim::fmir::BBReordering{}.apply(funcs);
  foptim::utils::TempAlloc<void *>::reset();
}

void codegen(foptim::FVec<foptim::fmir::MFunc> &funcs,
             foptim::FVec<foptim::IRString> &decls,
             foptim::FVec<foptim::fmir::Global> &globals) {
  ZoneScopedN("Codegen");
  foptim::codegen::run(funcs, decls, globals);
  fmt::println("Done!");
}
} // namespace
