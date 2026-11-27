#pragma once
#include "utils/types.hpp"

namespace foptim::optim::cost_model {
// instruction shape classes, the granularity of uops.info measurements. a FIR
// instruction lowers to a short sequence of these (fir_lowering_estimate.hpp).
// COUNT stays last, Machine::table() indexes a flat array of it
enum class X86OpClass : u16 {
  ALU_RR,  // add/sub/and/or/xor/cmp, register-register
  ALU_RI,  // same, register-immediate
  ALU_RM,  // same, register-memory (the memory operand already folded a load)
  LEA,     // address computation that could not fold into a memory operand
  IMUL_RR,
  IMUL_RM,
  IDIV32,
  IDIV64,
  SHIFT_RI,  // shift/rotate by an immediate count
  SHIFT_RR,  // shift/rotate by a register count (goes through cl)
  CMOV,
  SETCC,
  CMP_JCC_FUSED,  // macro-fused compare + conditional jump, one uop
  JCC,            // conditional jump not preceded by a fusable compare
  JMP,
  MOV_RR,
  LOAD,
  STORE,
  POPCNT,
  LZCNT,
  TZCNT,
  BSWAP,
  CALL,
  FP_ALU,  // scalar float add/sub/mul
  FP_DIV,  // scalar float div/sqrt
  FP_CMP,
  VEC_ALU_128,
  VEC_ALU_256,
  VEC_SHUFFLE,
  // the vector classes below are measured on the 128 bit form unless noted,
  // wider vectors scale through the lane count of the OpDesc
  VEC_IMUL,           // 32 bit lane multiply
  VEC_FP_ALU,         // packed float add/sub/mul
  VEC_FP_DIV,         // packed float div/sqrt
  VEC_CROSS_SHUFFLE,  // ymm lane crossing permute
  VEC_BLEND,          // variable blend, vector select
  VEC_CMP,
  VEC_SHIFT,
  VEC_MINMAX,
  VEC_GATHER,      // one full 8 lane ymm dword gather
  VEC_MASK_LOAD,   // vpmaskmov ymm load
  VEC_MASK_STORE,  // vpmaskmov ymm store
  VEC_LOAD,        // unaligned ymm load
  VEC_STORE,
  VEC_INSERT,  // one lane insert from a gpr
  VEC_EXTRACT,
  VEC_BROADCAST,
  VEC_CVT,

  COUNT,
};

// one row of the cost table
struct TableRow {
  double latency;
  double recip_throughput;
  // bitmask of abstract issue ports
  u8 ports_mask;
  // micro op count
  u8 n_uops;
  // typical encoded bytes
  u8 size_bytes;
};

}  // namespace foptim::optim::cost_model
