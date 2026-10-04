#pragma once
#include "ir/context.hpp"
#include "ir/function.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

// Collects all functions of the module in the order they should have
TVec<fir::Function *> get_lowering_order(fir::Context &ctx);

}  // namespace foptim::fmir
