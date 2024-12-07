#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/types_ref.hpp"
#include "utils/stable_vec_ref.hpp"
#include "ir/use.hpp"

namespace foptim::fir {

class BBArgumentData : public Used {
public:
  BasicBlock _parent;
  TypeR _type;

  BBArgumentData(BasicBlock parent, TypeR type);
  [[nodiscard]] BasicBlock get_parent() const;
  [[nodiscard]] TypeR get_type() const;
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
