#pragma once
#include "ir/function.hpp"

namespace foptim::optim {
// check if a whole function can be turned into a vectorized version
// returns cost iff it is possible returns nothing otherwise
// lanes are optional to check for specific widths or if 0 checks if theres any
// instructiosn/control flow blocking
std::optional<i64> can_whole_function_vectorize(fir::Function& func,
                                                       u64 lanes = 0);

// vectorizes a whole function returns a refernce to the vectorized function
std::optional<fir::FunctionR> whole_function_vectorize(fir::Function& func, u64 n_lanes);
}  // namespace foptim::optim
