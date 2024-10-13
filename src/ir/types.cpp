#include "ir/types.hpp"

namespace foptim::fir {

u32 VectorType::get_size() const {
  return member_type->get_size() * member_number;
}

} // namespace foptim::fir
