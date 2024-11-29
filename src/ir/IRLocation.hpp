#pragma once
#include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/function.hpp"
#include "ir/function_ref.hpp"
#include "ir/instruction_data.hpp"
#include "utils/types.hpp"
#include <unordered_map>

namespace foptim::fir {

struct IRLocation {
  enum class LocationType : u8 {
    INVALID = 0,
    Function,
    BasicBlock,
    Instruction,
  };

  LocationType type;
  u32 bb;
  u32 instr;
  FunctionR func;

  constexpr bool operator==(const IRLocation &other) const {
    return type == other.type && (func.func == other.func.func) &&
           bb == other.bb;
  }

  static u32 get_bb_indx(fir::BasicBlock search_bb) {
    const auto func = search_bb->get_parent();
    const auto &bbs = func->get_bbs();
    for (size_t bb_indx = 0; bb_indx < bbs.size(); bb_indx++) {
      if (bbs[bb_indx] == search_bb) {
        return bb_indx;
      }
    }
    ASSERT(false && "Tried to get IRLocation on bb that isnt in a function");
    std::abort();
  }
  static u32 get_instr_indx(fir::Instr search_instr) {
    const auto bb = search_instr->get_parent();
    const auto &instrs = bb->instructions;
    for (size_t instr_indx = 0; instr_indx < instrs.size(); instr_indx++) {
      if (instrs[instr_indx] == search_instr) {
        return instr_indx;
      }
    }
    ASSERT(false && "Tried to get IRLocation on bb that isnt in a function");
    std::abort();
  }

  IRLocation() : type(LocationType::INVALID), func(nullptr) {}
  ~IRLocation() = default;

  static IRLocation after(fir::Instr after_instr) {
    IRLocation loc(after_instr);
    loc.instr++;
    return loc;
  }

  constexpr IRLocation(fir::Instr from_instr)
      : type(LocationType::Instruction),
        func(from_instr->get_parent()->get_parent()) {
    bb = get_bb_indx(from_instr->get_parent());
    instr = get_instr_indx(from_instr);
  }

  constexpr IRLocation(fir::BasicBlock from_bb)
      : type(LocationType::BasicBlock), instr(0), func(from_bb->get_parent()) {
    bb = get_bb_indx(from_bb);
  }
};
} // namespace foptim::fir

template <> struct std::hash<foptim::fir::IRLocation::LocationType> {
  std::size_t
  operator()(const foptim::fir::IRLocation::LocationType &k) const noexcept {
    return hash<foptim::u8>()((foptim::u8)k);
  }
};

template <> struct std::hash<foptim::fir::IRLocation> {
  std::size_t operator()(const foptim::fir::IRLocation &k) const noexcept {
    return hash<foptim::u32>()(k.bb) ^ hash<foptim::u32>()(k.instr) ^
           hash<foptim::fir::IRLocation::LocationType>()(k.type);
  }
};
