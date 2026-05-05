#pragma once
#include <span>

#include "config/compiler_config.hpp"
#include "mir/global.hpp"
#include "utils/string.hpp"
#include "utils/types.hpp"

namespace foptim::codegen {

void run(std::span<const fmir::MFunc> funcs, std::span<const IRString> decls,
         std::span<const fmir::Global> globals, const conf::CompConf &conf);

}  // namespace foptim::codegen
