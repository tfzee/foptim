#pragma once
#include "optim/module_pass.hpp"

namespace foptim::optim {

class GarbageCollect final : public ModulePass {
public:
  void apply(fir::Context &ctx) override {
    ctx.data->storage.storage_instr.collect_garbage();
    // ctx.data->storage.storage_global.collect_garbage();
    ctx.data->storage.storage_constant.collect_garbage();
    // ctx.data->storage.storage_type.collect_garbage();
  }
};
} // namespace foptim::optim
