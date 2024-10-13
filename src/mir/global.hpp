#pragma once
#include "utils/types.hpp"
#include "utils/vec.hpp"
#include <string>

namespace foptim::fmir {

struct Global {
  std::string name;
  FVec<u8> data;
};

} // namespace foptim::fmir
