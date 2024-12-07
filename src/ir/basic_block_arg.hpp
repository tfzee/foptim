#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/types_ref.hpp"
#include "ir/use.hpp"
#include "utils/stable_vec_ref.hpp"
#include "utils/types.hpp"

namespace foptim::fir {

class BBArgumentData : public Used {
public:
  BasicBlock _parent;
  TypeR _type;

  constexpr BBArgumentData(BasicBlock parent, TypeR type)
      : Used(), _parent(parent), _type(type) {}

  [[nodiscard]] constexpr BasicBlock get_parent() const { return _parent; }
  [[nodiscard]] constexpr TypeR get_type() const { return _type; }
};

class BBArgument : public utils::SRef<BBArgumentData> {
public:
  constexpr bool operator==(const BBArgument &other) const {
    return utils::SRef<BBArgumentData>::operator==(other);
  }
  constexpr explicit BBArgument(utils::SRef<BBArgumentData> &&crtp) {
    this->data_ref = crtp.data_ref;
#ifdef SLOT_CHECK_GENERATION
    this->generation = crtp.generation;
#endif
  }
};

} // namespace foptim::fir

template <> struct std::hash<foptim::fir::BBArgument> {
  std::size_t operator()(const foptim::fir::BBArgument &k) const {
    using foptim::u32;
    using std::hash;
    return hash<foptim::utils::SRef<foptim::fir::BBArgumentData>>()(k);
  }
};
