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
  void apply(fir::Context &ctx) override {
    ZoneScopedN("GDCE");

    // TODO: dead global variable deletion
    for (auto &[name, f] : ctx.data->storage.functions) {
      if (f.get_n_uses() > 0) {
        continue;
      }
      if (f.linkage != foptim::fir::Function::Linkage::Internal) {
        continue;
      }
      if (name.starts_with("_GLOBAL")) {
        continue;
      }

      ctx.data->storage.functions.erase(name);
    }
  }
};
} // namespace foptim::optim
