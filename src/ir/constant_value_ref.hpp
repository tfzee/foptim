#pragma once
#include "../utils/stable_vec_ref.hpp"

namespace foptim::fir {
struct ConstantValue;

class ConstantValueR : public utils::SRef<ConstantValue> {
 public:
  explicit constexpr ConstantValueR(utils::SRef<ConstantValue> &&crtp) {
    this->data_ref = crtp.data_ref;
#ifdef SLOT_CHECK_GENERATION
    this->generation = crtp.generation;
#endif
  }
};
}  // namespace foptim::fir

template <>
struct std::hash<foptim::fir::ConstantValueR> {
  std::size_t operator()(const foptim::fir::ConstantValueR &k) const {
    return hash<foptim::utils::SRef<foptim::fir::ConstantValue>>()(k);
  }
};
