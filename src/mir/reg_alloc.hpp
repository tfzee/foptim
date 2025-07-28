#pragma once
#include "ir/value.hpp"
#include "mir/instr.hpp"
#include "utils/map.hpp"

struct IndexedValue {
  foptim::fir::ValueR v;
  foptim::u32 id;

  [[nodiscard]] constexpr bool operator==(const IndexedValue &other) const {
    return v == other.v && id == other.id;
  }
};
template <>
struct std::hash<IndexedValue> {
  std::size_t operator()(const IndexedValue &k) const {
    using foptim::u32;
    using foptim::fir::ValueR;
    return hash<ValueR>()(k.v) ^ hash<u32>()(k.id);
  }
};

namespace foptim::fmir {

class DumbRegAlloc {
  u64 vreg_num = 1;
  TMap<IndexedValue, VReg> mapping;

 public:
  VReg get_new_register(Type type);
  constexpr void reset() {
    mapping.clear();
    vreg_num = 1;
  }
  constexpr DumbRegAlloc() { reset(); }
  // void alloc_func(fir::Function &, optim::LiveVariables &lives);
  VReg get_register(fir::ValueR valu);
  VReg get_struct_register(fir::ValueR valu, fir::TypeR t, u32 id);
};

}  // namespace foptim::fmir
