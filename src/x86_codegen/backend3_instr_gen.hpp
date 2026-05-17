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

inline u64 emit_impl(u8 *buff, u32 curr_off, ZydisEncoderRequest *req,
                     int line) {
  u64 len = 9999;
  (void)line;
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

size_t emit_instr(const fmir::MInstr &instr, u8 *const out_buff, u8 curr_bb_id,
                  TLabelUsageMap &reloc_map, ProEpilogueType proepiloguetype,
                  const foptim::conf::CompConf &conf);
} // namespace foptim::codegen
