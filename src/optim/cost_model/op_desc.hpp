#pragma once
#include "optim/cost_model/cost.hpp"
#include "optim/cost_model/machine.hpp"

namespace foptim::optim::cost_model {
enum class OpKind : u8 {
  IntAlu,
  IntMul,
  IntDiv,
  Shift,
  FloatAddSub,
  FloatMul,
  FloatDiv,
  FloatSqrt,
  // compare producing a mask or flag
  Cmp,
  Select,
  MinMax,
  Load,
  Store,
  MaskedLoad,
  MaskedStore,
  // lanes loads from independent addresses
  Gather,
  // shuffle inside a 128 bit lane
  Shuffle,
  // shuffle that crosses the 128 bit lanes of a ymm
  CrossShuffle,
  Broadcast,
  // one lane in or out of a general purpose register
  Insert,
  Extract,
  // int <-> float of the same width
  Convert,
  // zext / sext / trunc between element widths
  Resize,
};

// an operation without an instruction, what the vectorizer prices before it
// emits anything. lanes 1 is the scalar form, elem_bits and lanes together give
// the vector width. Resize and Convert use elem_bits of the wider side
struct OpDesc {
  OpKind kind = OpKind::IntAlu;
  u32 elem_bits = 32;
  u32 lanes = 1;
  bool is_float = false;
  // the second operand is a constant (shifts by an immediate)
  bool const_rhs = false;
};

// cost of desc in the unit of kind on machine m. a vector wider than the
// largest register is the cost of the parts it is split into
Cost cost(const OpDesc &desc, CostKind kind, const Machine &m);
}  // namespace foptim::optim::cost_model
