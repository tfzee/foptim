#pragma once
#include "ir/value.hpp"
#include "utils/types.hpp"

namespace foptim::optim {

enum class AAResult : u8 {
  Alias,
  MightAlias,
  NoAlias,
};

inline AAResult basic_alias_test(fir::ValueR v1, fir::ValueR v2) {
  if (v1 == v2) {
    return AAResult::Alias;
  }
  return AAResult::NoAlias;
}

} // namespace foptim::optim
