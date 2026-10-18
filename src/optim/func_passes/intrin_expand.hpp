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

  std::optional<fir::BasicBlock> expand_memset(fir::Function &func,
                                               fir::BasicBlock bb,
                                               fir::Instr instr);
  // the element type is the extra_type of the intrinsic (bytes if not set)
  std::optional<fir::BasicBlock> expand_memcpy(fir::Function &func,
                                               fir::BasicBlock bb,
                                               fir::Instr instr);

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
        auto r = expand_memset(func, *bb_iter, i);
        if (!r) {
          r = expand_memcpy(func, *bb_iter, i);
        }
        if (r) {
          // blocks were appended, the old iterator is gone and the tail of
          // the split block can hold more intrinsics
          bb_iter = func.basic_blocks.begin();
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

}  // namespace foptim::optim

namespace foptim::optim {

/*
memmove and memcmp stay intrinsics until the very end so the passes in between
can fold them, then they become calls of the libc function (declared in the
module already, see generate_memmove). Has to run after the last pass that
creates them.
*/
class IntrinLowerCalls final : public FunctionPass {
 public:
  class Config {};

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override;
};

}  // namespace foptim::optim
