#pragma once
#include "ir/helpers.hpp"
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
  // data .empty -> declaration
  FVec<u8> data;
  u64 size = 0;
  IRVec<RelocationInfo> reloc_info;
  fir::LinkVisibility vis;
};

}  // namespace foptim::fmir
