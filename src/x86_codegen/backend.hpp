#pragma once
#include "mir/func.hpp"
#include "mir/global.hpp"
#include <span>
#include <vector>

namespace foptim::codegen {

void run(std::span<const fmir::MFunc> funcs, std::span<const fmir::Global> globals);

} // namespace foptim::codegen
