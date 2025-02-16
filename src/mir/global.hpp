#pragma once
#include "utils/string.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

struct Global {
  struct RelocationInfo {
    size_t offset;
    IRString name;
  };
  IRString name;
  //TODO: alignment
  FVec<u8> data;
  IRVec<RelocationInfo> reloc_info;
};

} // namespace foptim::fmir
