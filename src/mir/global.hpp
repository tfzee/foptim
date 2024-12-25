#pragma once
#include "utils/string.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

struct Global {
  IRString name;
  FVec<u8> data;
};

} // namespace foptim::fmir
