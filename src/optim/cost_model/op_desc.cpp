#include "optim/cost_model/op_desc.hpp"

#include <algorithm>
#include <cmath>

namespace foptim::optim::cost_model {
namespace {
struct Sum {
  const Machine &m;
  double latency = 0.0;
  double rtput = 0.0;
  double size = 0.0;

  void add(X86OpClass cls, double count = 1.0) {
    const TableRow &row = m.table(cls);
    latency += row.latency * count;
    rtput += row.recip_throughput * count;
    size += row.size_bytes * count;
  }
};

// registers a vector of bits takes, a zmm is two ymm halves where it is double pumped
double vec_parts(const Machine &m, u32 bits) {
  if (bits <= 256) {
    return 1.0;
  }
  if (m.isa != nullptr && m.isa->avx512f) {
    return std::ceil(bits / 512.0) * m.core().zmm_parts;
  }
  return std::ceil(bits / 256.0);
}

X86OpClass vec_alu(u32 bits) {
  return bits > 128 ? X86OpClass::VEC_ALU_256 : X86OpClass::VEC_ALU_128;
}

void scalar(const OpDesc &d, Sum &s) {
  switch (d.kind) {
    case OpKind::IntAlu:
      s.add(d.const_rhs ? X86OpClass::ALU_RI : X86OpClass::ALU_RR);
      return;
    case OpKind::IntMul:
      s.add(X86OpClass::IMUL_RR);
      return;
    case OpKind::IntDiv:
      s.add(d.elem_bits > 32 ? X86OpClass::IDIV64 : X86OpClass::IDIV32);
      return;
    case OpKind::Shift:
      s.add(d.const_rhs ? X86OpClass::SHIFT_RI : X86OpClass::SHIFT_RR);
      return;
    case OpKind::FloatAddSub:
    case OpKind::FloatMul:
    case OpKind::Convert:
      s.add(X86OpClass::FP_ALU);
      return;
    case OpKind::FloatDiv:
    case OpKind::FloatSqrt:
      s.add(X86OpClass::FP_DIV);
      return;
    case OpKind::Cmp:
      s.add(d.is_float ? X86OpClass::FP_CMP : X86OpClass::SETCC);
      return;
    case OpKind::Select:
      s.add(X86OpClass::CMOV);
      return;
    case OpKind::MinMax:
      if (d.is_float) {
        s.add(X86OpClass::FP_CMP);
      } else {
        s.add(X86OpClass::ALU_RR);
        s.add(X86OpClass::CMOV);
      }
      return;
    case OpKind::Load:
    case OpKind::Gather:
    case OpKind::MaskedLoad:
      s.add(X86OpClass::LOAD);
      return;
    case OpKind::Store:
    case OpKind::MaskedStore:
      s.add(X86OpClass::STORE);
      return;
    case OpKind::Shuffle:
    case OpKind::CrossShuffle:
    case OpKind::Broadcast:
    case OpKind::Insert:
    case OpKind::Extract:
      s.add(X86OpClass::MOV_RR);
      return;
    case OpKind::Resize:
      s.add(X86OpClass::MOV_RR);
      return;
  }
}

// one register part of a vector op of bits
void vector_part(const Machine &m, const OpDesc &d, u32 bits, Sum &s) {
  const bool dq = m.isa != nullptr && m.isa->avx512dq;
  switch (d.kind) {
    case OpKind::IntAlu:
      s.add(vec_alu(bits));
      return;
    case OpKind::IntMul:
      if (d.elem_bits == 64) {
        if (dq) {
          s.add(X86OpClass::VEC_IMUL, 3.0);
        } else {
          // three 32x32 multiplies, the shifts and adds of the cross terms
          s.add(X86OpClass::VEC_IMUL, 3.0);
          s.add(X86OpClass::VEC_SHIFT, 2.0);
          s.add(vec_alu(bits), 2.0);
        }
      } else if (d.elem_bits == 8) {
        s.add(X86OpClass::VEC_IMUL, 2.0);
        s.add(vec_alu(bits), 4.0);
      } else {
        s.add(X86OpClass::VEC_IMUL);
      }
      return;
    case OpKind::IntDiv:
      // no vector divide, every lane runs the scalar one
      for (u32 i = 0; i < d.lanes; i++) {
        scalar(d, s);
      }
      return;
    case OpKind::Shift:
      s.add(X86OpClass::VEC_SHIFT, d.elem_bits == 8 ? 3.0 : 1.0);
      return;
    case OpKind::FloatAddSub:
    case OpKind::FloatMul:
      s.add(X86OpClass::VEC_FP_ALU);
      return;
    case OpKind::FloatDiv:
    case OpKind::FloatSqrt:
      s.add(X86OpClass::VEC_FP_DIV);
      return;
    case OpKind::Cmp:
      s.add(X86OpClass::VEC_CMP);
      return;
    case OpKind::Select:
      s.add(X86OpClass::VEC_BLEND);
      return;
    case OpKind::MinMax:
      if (!d.is_float && d.elem_bits == 64 && !m.isa->avx512f) {
        s.add(X86OpClass::VEC_CMP);
        s.add(X86OpClass::VEC_BLEND);
      } else {
        s.add(X86OpClass::VEC_MINMAX);
      }
      return;
    case OpKind::Load:
      s.add(X86OpClass::VEC_LOAD);
      return;
    case OpKind::Store:
      s.add(X86OpClass::VEC_STORE);
      return;
    case OpKind::MaskedLoad:
      s.add(X86OpClass::VEC_MASK_LOAD);
      return;
    case OpKind::MaskedStore:
      s.add(X86OpClass::VEC_MASK_STORE);
      return;
    case OpKind::Gather:
      // the row is a full 8 lane gather
      s.add(X86OpClass::VEC_GATHER, std::min(1.0, d.lanes / 8.0));
      s.add(X86OpClass::VEC_BLEND);
      return;
    case OpKind::Shuffle:
      s.add(X86OpClass::VEC_SHUFFLE);
      return;
    case OpKind::CrossShuffle:
      s.add(X86OpClass::VEC_CROSS_SHUFFLE);
      return;
    case OpKind::Broadcast:
      s.add(X86OpClass::VEC_BROADCAST);
      return;
    case OpKind::Insert:
      s.add(X86OpClass::VEC_INSERT);
      return;
    case OpKind::Extract:
      s.add(X86OpClass::VEC_EXTRACT);
      return;
    case OpKind::Convert:
      s.add(X86OpClass::VEC_CVT);
      return;
    case OpKind::Resize:
      // vpmovzx / vpmovsx, a lane crossing one when the result needs a ymm
      s.add(bits > 128 ? X86OpClass::VEC_CROSS_SHUFFLE
                       : X86OpClass::VEC_SHUFFLE);
      return;
  }
}
}  // namespace

Cost cost(const OpDesc &desc, CostKind kind, const Machine &m) {
  Sum s{m};
  if (desc.lanes <= 1) {
    scalar(desc, s);
  } else {
    const u32 bits = desc.elem_bits * desc.lanes;
    const double parts = vec_parts(m, bits);
    Sum one{m};
    vector_part(m, desc, std::min(bits, 256u), one);
    // the parts run side by side, only the throughput and the size add up
    s.latency = one.latency;
    s.rtput = one.rtput * parts;
    s.size = one.size * parts;
  }
  return combine_cost(kind, s.latency, s.rtput, s.size, m.tune().size_weight);
}

}  // namespace foptim::optim::cost_model
