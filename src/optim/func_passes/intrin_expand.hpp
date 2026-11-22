#pragma once
/*
Try to convert intrinsics like memcpy back into proper loops cause then they
might get better optimized combined with sorounding code
*/
#include "ir/function.hpp"
#include "optim/function_pass.hpp"
#include "optim/helper/sat_expand.hpp"
namespace foptim::optim {

class IntrinExpand final : public FunctionPass {
  using iter = FVec<fir::Instr>::iterator;

  std::optional<fir::BasicBlock> expand_memset(fir::Function &func,
                                               fir::BasicBlock bb,
                                               fir::Instr instr);
  // the element type is the extra_type of the intrinsic (bytes if not set)
  std::optional<fir::BasicBlock> expand_memcpy(fir::Function &func,
                                               fir::BasicBlock bb,
                                               fir::Instr instr);
  // atomicrmw ops without a single x86 instruction become a cmpxchg loop
  std::optional<fir::BasicBlock> expand_atomic_rmw(fir::Function &func,
                                                   fir::BasicBlock bb,
                                                   fir::Instr instr);

 public:
  class Config {};

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedNC("IntrinExpand", COLOR_OPTIMF);

    bool modified = false;
    // saturating scalar arithmetic has no instruction, it is a compare and a
    // select
    for (auto bb : func.basic_blocks) {
      TVec<fir::Instr> sats;
      for (auto i : bb->instructions) {
        if (i->is(fir::InstrType::Intrinsic) &&
            is_sat_intrinsic(static_cast<fir::IntrinsicSubType>(i->subtype)) &&
            !sat_is_native(i.result_type())) {
          sats.push_back(i);
        }
      }
      for (auto i : sats) {
        fir::Builder b{i};
        const auto res =
            expand_sat(ctx, b, static_cast<fir::IntrinsicSubType>(i->subtype),
                       i->args[0], i->args[1]);
        i->replace_all_uses(res);
        i.destroy();
        modified = true;
      }
    }
    auto bb_iter = func.basic_blocks.begin();
    while (bb_iter != func.basic_blocks.end()) {
      bool broke = false;
      for (auto i : (*bb_iter)->instructions) {
        std::optional<fir::BasicBlock> r;
        if (i->is(fir::InstrType::AtomicRMW)) {
          r = expand_atomic_rmw(func, *bb_iter, i);
        } else if (i->is(fir::InstrType::Intrinsic)) {
          r = expand_memset(func, *bb_iter, i);
          if (!r) {
            r = expand_memcpy(func, *bb_iter, i);
          }
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
