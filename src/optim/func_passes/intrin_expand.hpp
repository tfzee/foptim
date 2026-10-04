#pragma once
/*
Try to convert intrinsics like memcpy back into proper loops cause then they
might get better optimized combined with sorounding code
*/
#include "ir/function.hpp"
#include "optim/function_pass.hpp"
namespace foptim::optim {

class IntrinExpand final : public FunctionPass {
  using iter = IRVec<fir::Instr>::iterator;

  std::optional<fir::BasicBlock>
  expand_memset(fir::Function &func, fir::BasicBlock bb, fir::Instr instr);

public:
  class Config {};

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {

    (void)ctx;
    (void)func;
    ZoneScopedNC("IntrinExpand", COLOR_OPTIMF);

    bool modified = false;
    auto bb_iter = func.basic_blocks.begin();
    while (bb_iter != func.basic_blocks.end()) {
      bool broke = false;
      for (auto i : (*bb_iter)->instructions) {
        if (!i->is(fir::InstrType::Intrinsic)) {
          continue;
        }
        if (auto r = expand_memset(func, *bb_iter, i)) {
          bb_iter = std::ranges::find(func.basic_blocks, r);
          modified = true;
          broke = true;
          break;
        }
      }
      if (!broke) {
        bb_iter++;
      }
    }

    return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
  }
};

} // namespace foptim::optim
