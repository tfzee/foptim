#pragma once
#include "ir/constant_value_ref.hpp"
// #include "types_ref.hpp"
#include "utils/stable_vec_ref.hpp"

namespace foptim::fir {

struct GlobalData {
  // TypeR type;
  size_t n_bytes;
  ConstantValueR init_value;
};

struct Global : public utils::SRef<GlobalData> {
public:
  constexpr explicit Global(utils::SRef<GlobalData> &&crtp) {
    this->data_ref = crtp.data_ref;
#ifdef SLOT_CHECK_GENERATION
    this->generation = crtp.generation;
#endif
  }
};

}; // namespace foptim::fir
