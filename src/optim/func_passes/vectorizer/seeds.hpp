#pragma once
#include <memory>

#include "optim/func_passes/vectorizer/plan.hpp"

namespace foptim::optim::vec {

// A horizontal reduction chain that ends in the instruction at instr_id.
std::optional<Seed> find_reduction(fir::BasicBlock bb, size_t instr_id,
                                   const conf::CompConf &conf);

// Stores that write adjacent memory.
std::unique_ptr<SeedProvider> make_store_seeds();
// Chains of associative operations over a few values.
std::unique_ptr<SeedProvider> make_reduction_seeds();

}  // namespace foptim::optim::vec
