#pragma once
#include "utils/string.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

struct Global {
  struct RelocationInfo {
    size_t insert_offset;
    IRStringRef name;
    size_t reloc_offset = 0;
  };

  IRStringRef name;
  // TODO: alignment
  FVec<u8> data;
  IRVec<RelocationInfo> reloc_info;
};

} // namespace foptim::fmir
