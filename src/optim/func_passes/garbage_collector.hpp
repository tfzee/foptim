#pragma once
#include "ir/function.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {

class GarbageCollect final : public FunctionPass {
public:
  void apply(fir::Context &ctx, fir::Function & /*unused*/) override {
    ctx.data->storage.storage_instr.collect_garbage();
    ctx.data->storage.storage_constant.collect_garbage();
    ctx.data->storage.storage_global.collect_garbage();
    ctx.data->storage.storage_type.collect_garbage();
  }
};
} // namespace foptim::optim
