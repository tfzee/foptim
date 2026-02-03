#pragma once
#include <fmt/core.h>

#include "utils/tracy.hpp"

#include "optim/module_pass.hpp"

namespace foptim::optim {

void merge_func_dups(fir::Context &ctx, JobSheduler *shed);
void merge_func_dups_only_same(fir::Context &ctx);

template <bool onlySame>
class FunctionDeDup final : public ModulePass {
 public:
  void apply(fir::Context &ctx, JobSheduler *shed) override {
    ZoneScopedN("FunctionDeDup");
    // TODO maybe run always onlysame before hand ??
    // but then we iterate twice over everything??
    if constexpr (onlySame) {
      merge_func_dups_only_same(ctx);
    } else {
      merge_func_dups(ctx, shed);
    }
  }
};
}  // namespace foptim::optim
