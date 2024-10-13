#pragma once
#include "mir/func.hpp"
#include "mir/global.hpp"
#include <vector>

namespace foptim::codegen {

void run(const FVec<fmir::MFunc> &funcs,
         const FVec<fmir::Global> &globals);

} // namespace foptim::codegen
