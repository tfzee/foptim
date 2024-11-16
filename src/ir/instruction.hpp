#pragma once
#include "../utils/stable_vec_ref.hpp"
#include "utils/map.hpp"

namespace foptim::fir {
class InstrData;
class Use;
class ValueR;
class BasicBlock;
class TypeR;

class Instr : public utils::SRef<InstrData> {
public:
  constexpr explicit Instr(utils::SRef<InstrData> &&crtp) {
    this->data_ref = crtp.data_ref;
#ifdef SLOT_CHECK_GENERATION
    this->generation = crtp.generation;
#endif
  }

  [[nodiscard]] TypeR get_type() const;
  void clear_args();
  void clear_bbs();
  void clear_bb_args(u16 indx);

  u16 add_arg(ValueR val);
  u16 add_bb(BasicBlock val);
  u16 add_bb_arg(u16 bb, ValueR val);
  u16 add_bb_arg(BasicBlock target, ValueR val);
  [[nodiscard]] u16 get_bb_id(BasicBlock target) const;
  ValueR replace_arg(u16 indx, ValueR new_val);
  BasicBlock replace_bb(u16 indx, BasicBlock new_val, bool keepArgs = true);
  BasicBlock replace_bb(BasicBlock target, BasicBlock new_val,
                        bool keepArgs = true);
  ValueR replace_bb_arg(BasicBlock target, u16 indx, ValueR new_val);
  ValueR replace_bb_arg(u16 bb_id, u16 indx, ValueR new_val);

  /*
  Substitutes args, bbs and bbargs if they are in the map
  @returns true if it replaced any
  */
  bool substitute(const FMap<ValueR, ValueR> &repl);
  bool substitute(const TMap<ValueR, ValueR> &repl);

  /*
  Removes it from the parent basicblock and cleans up references to it
  aswell as uses of its arguments. Does not delete the instruction.
  */
  void remove_from_parent();
};

} // namespace foptim::fir

template <> struct std::hash<foptim::fir::Instr> {
  std::size_t operator()(const foptim::fir::Instr &k) const {
    using foptim::u32;
    using std::hash;
    return hash<foptim::utils::SRef<foptim::fir::InstrData>>()(k);
  }
};
// template <> struct std::hash<foptim::fir::Instr> {
//   std::size_t operator()(const foptim::fir::Instr &k) const {
//     return
//   }
// };
