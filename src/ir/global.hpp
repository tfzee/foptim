#pragma once
#include "ir/constant_value_ref.hpp"
// #include "types_ref.hpp"
#include "utils/stable_vec_ref.hpp"
#include "utils/vec.hpp"

namespace foptim::fir {
struct GlobalData;

struct Global : public utils::SRef<GlobalData> {
public:
  constexpr explicit Global(utils::SRef<GlobalData> &&crtp) {
    this->data_ref = crtp.data_ref;
#ifdef SLOT_CHECK_GENERATION
    this->generation = crtp.generation;
#endif
  }
};

struct GlobalData {
  struct RelocationInfo {
    size_t offset;
    ConstantValueR ref;
  };
  // TypeR type;
  size_t n_bytes;
  uint8_t *init_value = nullptr;
  IRVec<RelocationInfo> reloc_info;
};

}; // namespace foptim::fir
