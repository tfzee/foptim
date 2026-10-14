#pragma once
#include <fmt/base.h>

#include "third_party/Zydis.h"
#include "x86_codegen/backend3.hpp"
#include "x86_codegen/backend3_print.hpp"

namespace foptim::codegen {

#define ZY_ASS(status)                                                         \
  do {                                                                         \
    const ZyanStatus status_047620348 = (status);                              \
    if (!ZYAN_SUCCESS(status_047620348)) {                                     \
      fmt::println("Zyan op failed: {} in module: {}",                         \
                   ZYAN_STATUS_CODE(status_047620348),                         \
                   ZYAN_STATUS_MODULE(status_047620348));                      \
      TODO("");                                                                \
    }                                                                          \
  } while (0)

#define ZY_ASS_REQ(status, req)                                                \
  do {                                                                         \
    const ZyanStatus status_047620348 = (status);                              \
    if (!ZYAN_SUCCESS(status_047620348)) {                                     \
      fmt::println("{}", req);                                                 \
      fmt::println("Zyan op failed: {} in module: {}",                         \
                   ZYAN_STATUS_CODE(status_047620348),                         \
                   ZYAN_STATUS_MODULE(status_047620348));                      \
      TODO("");                                                                \
    }                                                                          \
  } while (0)

#define emit(buff, off, req) emit_impl(buff, off, req, __LINE__)

// The encoder lists the write mask of an evex instruction as an operand right
// after the destination. Plain avx512 forms (zmm without masking) get k0, so do
// the instructions that only exist as evex (vpabsq) on smaller registers.
inline void add_zmm_mask_operand(ZydisEncoderRequest *req) {
  bool has_zmm = req->mnemonic == ZYDIS_MNEMONIC_VPABSQ;
  for (u8 i = 0; i < req->operand_count; i++) {
    const auto &op = req->operands[i];
    if (op.type == ZYDIS_OPERAND_TYPE_REGISTER &&
        op.reg.value >= ZYDIS_REGISTER_ZMM0 &&
        op.reg.value <= ZYDIS_REGISTER_ZMM31) {
      has_zmm = true;
    }
  }
  if (!has_zmm || req->operand_count < 2 || req->operand_count >= 5) {
    return;
  }
  const auto &second = req->operands[1];
  if (second.type == ZYDIS_OPERAND_TYPE_REGISTER &&
      second.reg.value >= ZYDIS_REGISTER_K0 &&
      second.reg.value <= ZYDIS_REGISTER_K7) {
    return;
  }
  for (u8 i = req->operand_count; i > 1; i--) {
    req->operands[i] = req->operands[i - 1];
  }
  req->operands[1] = ZydisEncoderOperand{};
  req->operands[1].type = ZYDIS_OPERAND_TYPE_REGISTER;
  req->operands[1].reg.value = ZYDIS_REGISTER_K0;
  req->operand_count++;
}

inline u64 emit_impl(u8 *buff, u32 curr_off, ZydisEncoderRequest *req,
                     int line) {
  u64 len = 9999;
  (void)line;
  add_zmm_mask_operand(req);
  const ZyanStatus status =
      (ZydisEncoderEncodeInstruction(req, buff + curr_off, &len));
  if (!ZYAN_SUCCESS(status)) {
    fmt::println("{}", *req);
    fmt::println("Zyan op failed: {} in module: {}", ZYAN_STATUS_CODE(status),
                 ZYAN_STATUS_MODULE(status));
    foptim ::todo_impl(
        "", "/home/tim/programming/foptim/src/x86_codegen/backend3.cpp", line);
  }
  return curr_off + len;
}

size_t emit_instr(const fmir::MInstr &instr, u8 *const out_buff, u32 curr_bb_id,
                  TLabelUsageMap &reloc_map, ProEpilogueType proepiloguetype,
                  const foptim::conf::CompConf &conf);
} // namespace foptim::codegen
