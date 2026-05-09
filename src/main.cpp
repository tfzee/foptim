#include <fmt/base.h>
#include <fmt/core.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>

#include "arg_parsing/parser.hpp"
#include "config/compiler_config.hpp"
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
#include "optim/fir_pipeline.hpp"
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
void lower_to_mir_and_optimize(foptim::fir::Context &ctx,
                               foptim::FVec<foptim::fmir::MFunc> &funcs,
                               foptim::FVec<foptim::fmir::Global> &globals,
                               foptim::JobSheduler *shed);
void codegen(foptim::FVec<foptim::fmir::MFunc> &funcs,
             foptim::FVec<foptim::IRString> &decls,
             foptim::FVec<foptim::fmir::Global> &globals,
             const foptim::conf::CompConf &conf);

}  // namespace

int main(int argc, char *argv[]) {
  ZoneScopedN("BASE");
  foptim::conf::CompConf conf;
  parse_args(argc, argv, conf);
  foptim::utils::DumbTimer t;
  foptim::JobSheduler shed;
  shed.init(foptim::utils::number_worker_threads);
  if (foptim::utils::verbosity > 0) {
    fmt::println("Running with {} Workers",
                 foptim::utils::number_worker_threads);
  }

  {
    auto a1 = t.scopedTimer("CompileTime");

    foptim::fir::Context ctx(&conf);
    {
      // fir
      {
        auto a1 = t.scopedTimer("ParseConvert");
        parse_llvm_ir(ctx, shed);
      }
      {
        auto a1 = t.scopedTimer("Optimize");
        foptim::conf::pipeline::optimize_fir(ctx, &shed);
        // optimize_fir(ctx, &shed);
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
      codegen(funcs, decls, globals, conf);
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
    const auto *config = ctx.config;
    shed->push(nullptr, [i, &funcs, reord_func, config]() {
      auto &func = funcs.at(i);
      auto matcher = foptim::fmir::GreedyMatcher{};
      func = matcher.apply(*reord_func, *config);
      ASSERT(foptim::fmir::verify(func));
      foptim::fmir::LegalizeBBForm{}.apply(func, *config);
      foptim::fmir::DeadCodeElim{}.apply(func, *config);
      foptim::fmir::CopyPropagation{}.apply(func, *config);
      foptim::fmir::DeadCodeElim{}.apply(func, *config);
      foptim::fmir::BBReordering{}.apply(func, *config);
      foptim::fmir::DeadCodeElim{}.apply(func, *config);
      foptim::fmir::LVN{}.apply(func, *config);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::CopyPropagation{}.apply(func, *config);
      foptim::fmir::InstSimplifyEarly{}.apply(func, *config);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::LifetimeShortening{}.apply(func, *config);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::CopyPropagation{}.apply(func, *config);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::InstSimplifyEarly{}.apply(func, *config);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::DeadCodeElim{}.apply(func, *config);
      foptim::utils::TempAlloc<void *>::reset();
      ASSERT(foptim::fmir::verify(func));
      foptim::fmir::CallingConvFirst{}.apply(func, *config);
      foptim::fmir::Legalizer{}.apply(func, *config);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::InstSimplifyEarly{}.apply(func, *config);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::RegisterJoining{}.apply(func, *config);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::InstSimplifyEarly{}.apply(func, *config);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::RegAlloc{}.apply(func, *config);
      // fmt::println("{:cd}", func);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::CallingConvSecond{}.apply(func, *config);
      foptim::fmir::StackOptim{}.apply(func, *config);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::InstSimplify{}.apply(func, *config);
      foptim::fmir::InstSimplify{}.apply(func, *config);
      foptim::utils::TempAlloc<void *>::reset();
      foptim::fmir::BBReordering{}.apply(func, *config);
      foptim::utils::TempAlloc<void *>::reset();
      // fmt::println("{:cd}", func);
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
             foptim::FVec<foptim::fmir::Global> &globals,
             const foptim::conf::CompConf &conf) {
  ZoneScopedN("Codegen");
  foptim::codegen::run(funcs, decls, globals, conf);
  fmt::println("Done!");
}
}  // namespace
