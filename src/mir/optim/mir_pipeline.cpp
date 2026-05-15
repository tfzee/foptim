#include "mir_pipeline.hpp"
#include "config/compiler_config.hpp"
#include "config/compiler_mir_passes.hpp"
#include "ir/function.hpp"
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
#include "utils/todo.hpp"
#include <deque>
#include <fmt/base.h>
#include <ranges>

namespace foptim::fmir::pipeline {

void optimize_mir(foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::TVec<foptim::fir::Function *> &reordered_funcs,
                  foptim::FVec<foptim::fmir::Global> &,
                  foptim::JobSheduler *shed, const conf::CompConf &config) {
  size_t i = 0;
  // do matching first since it usese TVec we must before touching da tempalloc
  // reset
  for (auto *reord_func : reordered_funcs) {
    if (reord_func->is_decl()) {
      continue;
    }
    shed->push(nullptr, [i, &funcs, reord_func, &config]() {
      auto &func = funcs.at(i);
      auto matcher = foptim::fmir::GreedyMatcher{};
      func = matcher.apply(*reord_func, config);
      ASSERT(foptim::fmir::verify(func));
    });
    i++;
  }
  shed->wait_till_done();

  // TODO: use my types
  std::vector<conf::PassConfig *> passes_worklist;
  std::deque<conf::PipelineElem> pipeline_worklist;

  pipeline_worklist.push_back(conf::PipelineElem{config.optim.mir_pipeline});

  // construct full pipeline
  while (!pipeline_worklist.empty()) {
    auto curr_pipe = pipeline_worklist.back();
    pipeline_worklist.pop_back();
    if (curr_pipe.type == conf::PipelineElem::Pipeline) {
      for (auto elem : std::ranges::reverse_view(curr_pipe.pipeline->passes)) {
        pipeline_worklist.push_back(elem);
      }
    } else {
      passes_worklist.push_back(*curr_pipe.pass.get_raw_ptr());
    }
  }
  foptim::utils::TempAlloc<void *>::reset();
  fmt::println("Running {} MIR passes", passes_worklist.size());

  // TODO destruction of stuff kinda iffy
  for (auto &func : funcs) {
    ASSERT(!func.bbs.empty());
    shed->push(nullptr, [&func, &passes_worklist, &config]() {
      for (auto &pass_conf : passes_worklist) {
        auto pass = pass_conf->_construct_mir_func_pass();
        pass->apply(func, config);
      }
      // foptim::fmir::LegalizeBBForm{}.apply(func, config);
      // foptim::fmir::DeadCodeElim{}.apply(func, config);
      // foptim::fmir::CopyPropagation{}.apply(func, config);
      // foptim::fmir::DeadCodeElim{}.apply(func, config);
      // foptim::fmir::BBReordering{}.apply(func, config);
      // foptim::fmir::DeadCodeElim{}.apply(func, config);
      // foptim::fmir::LVN{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // foptim::fmir::CopyPropagation{}.apply(func, config);
      // foptim::fmir::InstSimplifyEarly{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // foptim::fmir::LifetimeShortening{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // foptim::fmir::CopyPropagation{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // foptim::fmir::InstSimplifyEarly{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // foptim::fmir::DeadCodeElim{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // ASSERT(foptim::fmir::verify(func));
      // foptim::fmir::CallingConvFirst{}.apply(func, config);
      // foptim::fmir::Legalizer{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // foptim::fmir::InstSimplifyEarly{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // foptim::fmir::RegisterJoining{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // foptim::fmir::InstSimplifyEarly{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // foptim::fmir::RegAlloc{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // foptim::fmir::CallingConvSecond{}.apply(func, config);
      // foptim::fmir::StackOptim{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // foptim::fmir::InstSimplify{}.apply(func, config);
      // foptim::fmir::InstSimplify{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // foptim::fmir::BBReordering{}.apply(func, config);
      // foptim::utils::TempAlloc<void *>::reset();
      // fmt::println("{}", func);
      ASSERT(foptim::fmir::verify(func));
    });
  }
  shed->wait_till_done();
}

} // namespace foptim::fmir::pipeline
