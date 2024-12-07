#pragma once
#include "utils/stable_vec_ref.hpp"
#include "utils/types.hpp"

namespace foptim::fir {
class BasicBlockData;
class BBArgument;
class Instr;
class Builder;
class TypeR;
class ValueR;

class BasicBlock : public utils::SRef<BasicBlockData> {
public:
  constexpr bool operator==(const BasicBlock &other) const {
    return utils::SRef<BasicBlockData>::operator==(other);
  }
  constexpr explicit BasicBlock(utils::SRef<BasicBlockData> &&crtp) {
    this->data_ref = crtp.data_ref;
#ifdef SLOT_CHECK_GENERATION
    this->generation = crtp.generation;
#endif
  }
  void insert_instr(size_t indx, Instr instr);
  void push_instr(Instr instr);
  BBArgument add_arg(BBArgument arg);
  Builder builder();
  Builder builder_at_end();
};
} // namespace foptim::fir

template <> struct std::hash<foptim::fir::BasicBlock> {
  std::size_t operator()(const foptim::fir::BasicBlock &k) const {
    using foptim::u32;
    using std::hash;
    return hash<foptim::utils::SRef<foptim::fir::BasicBlockData>>()(k);
  }
};
