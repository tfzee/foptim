#pragma once
#include "../utils/stable_vec_ref.hpp"

namespace foptim::fir {

class AnyType;

class TypeR : public utils::SRef<AnyType> {
public:
  constexpr explicit TypeR(utils::SRef<AnyType> &&crtp) {
    this->data_ref = crtp.data_ref;
#ifdef SLOT_CHECK_GENERATION
    this->generation = crtp.generation;
#endif
  }
};

typedef TypeR FunctionTypeR;
typedef TypeR IntTypeR;
typedef TypeR VoidTypeR;

} // namespace foptim::fir
