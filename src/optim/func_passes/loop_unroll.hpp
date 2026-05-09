#pragma once
#include "optim/analysis/loop_analysis.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {
struct LoopInfo;
class CFG;

class LoopUnroll final : public FunctionPass {
 public:
  struct Config {
    u32 max_unroll = 1024;
    u32 max_instr = 32;
  } config;
  void apply(fir::Context &ctx, fir::Function &func) override;
  bool apply_it(CFG &cfg, LoopInfo &loop, fir::Context &ctx,
                fir::Function &func, LoopBoundsAnalysis &lb);
};

void peel_it(CFG &cfg, LoopInfo &loop, u8 peel_factor, fir::Context &ctx,
             fir::Function &func, bool known_to_iterate_more);
void unroll_it(CFG &cfg, LoopInfo &loop, u8 unroll_factor, fir::Context &ctx,
               fir::Function &func, bool is_full_unroll);

}  // namespace foptim::optim
