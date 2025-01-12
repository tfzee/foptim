#pragma once
#include "utils/types.hpp"
#include "mir/global.hpp"
#include "utils/string.hpp"
#include <span>

namespace foptim::codegen {

void run(std::span<const fmir::MFunc> funcs, std::span<const IRString> decls,
         std::span<const fmir::Global> globals);

} // namespace foptim::codegen
