#pragma once
#include <fmt/base.h>
#include <fmt/core.h>

#include "ir/basic_block_arg.hpp"
#include "ir/function_ref.hpp"
#include "ir/helpers.hpp"
#include "ir/use.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/module_pass.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

namespace {
u64 arg_prom_unique_name_number = 0;
}

class ArgPromotion final : public ModulePass {
 public:
  bool are_there_potential_aliasing_stores(fir::FunctionR /*func*/,
                                           fir::BBArgument /*barg*/,
                                           fir::Use use, CFG &cfg,
                                           AliasAnalyis &aa);

  // if we return 2 vectors and then instant concat them we can also concat them
  // inside of the function
  bool return_vecvec_to_concat_vec(fir::FunctionR func, fir::Context &ctx);

  // if we get vector args and all we do is concat them concat them prior and
  // only give in 1 arg
  bool promote_vecvec_to_concat_vec(fir::FunctionR func, fir::Context &ctx);

  // if we return a pointer and its just loaded once just load on the inside so
  // we dont have pointers getting handed arround
  //  allos will allows for further optimizations possibly on the pointer that
  //  would otherwise escape
  bool promote_ptr_to_value_return(fir::FunctionR func, fir::Context &ctx);

  // if *some* load of the value *always* executes.
  // (dominates all exits)
  bool all_exits_are_dominated(const CFG &cfg, const Dominators &dom,
                               const TVec<u32> &direct_load_bbs);

  // if we have ptr arguments and all we do is a load of its value and its not a
  // massive value (<= ptrsize also good cause CC)
  //  we can isntead do the load before the call allowing potentialy more
  //  optimizations on that side like mem2reg
  bool promote_ptr_to_value_args(fir::FunctionR func, fir::Context &ctx);

  void apply(fir::Context &ctx, JobSheduler * /*unused*/) override;
};

}  // namespace foptim::optim
