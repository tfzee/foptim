#include "mir_pipeline.hpp"
#include "config/compiler_config.hpp"
#include "config/compiler_mir_passes.hpp"
#include "ir/function.hpp"
#include "mir/matcher.hpp"
#include "mir/optim/register_joining.hpp"
#include "utils/todo.hpp"
#include <deque>
#include <fmt/base.h>
#include <ranges>

namespace foptim::fmir::pipeline {

void optimize_mir(foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::TVec<foptim::fir::Function *> &reordered_funcs,
                  foptim::FVec<foptim::fmir::Global> & /*unused*/,
                  foptim::JobSheduler *shed, const conf::CompConf &config) {
  size_t i = 0;
  bool enable_bisect = config.debug.bisect >= 0;
  // do matching first since it usese TVec we must before touching da tempalloc
  // reset
  if (enable_bisect) {
    fmt::println("X 0: MATCHING");
  }
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

  pipeline_worklist.emplace_back(config.optim.mir_pipeline);

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

  // with bisect just disable parralelism
  if (enable_bisect) {
    size_t curr_pass = 1;
    for (auto &pass_conf : passes_worklist) {
      fmt::println("X {}: {}", curr_pass, pass_conf->get_name());
      for (auto &func : funcs) {
        ASSERT(!func.bbs.empty());
        auto *pass = pass_conf->_construct_mir_func_pass();
        pass->apply(func, config);
        ASSERT(foptim::fmir::verify(func));
      }
      curr_pass++;
    }
  } else {

    // TODO destruction of stuff kinda iffy
    for (auto &func : funcs) {
      ASSERT(!func.bbs.empty());
      shed->push(nullptr, [&func, &passes_worklist, &config]() {
        for (auto &pass_conf : passes_worklist) {
          // temp allocations are never freed individually, rewind after every
          // pass so they don't pile up over all passes (and functions)
          const auto temp_mark = foptim::utils::TempAlloc<void *>::save();
          auto *pass = pass_conf->_construct_mir_func_pass();
          pass->apply(func, config);
          foptim::utils::TempAlloc<void *>::restore(temp_mark);
        }
        ASSERT(foptim::fmir::verify(func));
      });
    }
    shed->wait_till_done();
  }
}

} // namespace foptim::fmir::pipeline
