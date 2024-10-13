#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"

namespace foptim::optim {

class LocalLoadStore final : public FunctionPass {
public:
  void apply(fir::BasicBlock){
  }

  
  void apply(fir::Context &, fir::Function &func) override {
    ZoneScopedN("LocalLoadStore");
    for (auto bb : func.basic_blocks) {
      apply(bb);
    }
  }
};
} // namespace foptim::optim
