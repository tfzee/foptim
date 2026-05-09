#pragma once
#include <fmt/base.h>

#include "third_party/Zydis.h"
#include "utils/logging.hpp"
#include "x86_codegen/backend3.hpp"

const char *get_reg_name(const ZydisRegister &data);

template <>
class fmt::formatter<ZydisEncoderOperand>
    : public BaseIRFormatter<ZydisEncoderOperand> {
public:
  appender format(ZydisEncoderOperand const &data, format_context &ctx) const {
    auto app = ctx.out();

    switch (data.type) {
    case ZYDIS_OPERAND_TYPE_UNUSED:
      return fmt::format_to(app, "UNUSED");
    case ZYDIS_OPERAND_TYPE_REGISTER:
      return fmt::format_to(app, "{}", get_reg_name(data.reg.value));
    case ZYDIS_OPERAND_TYPE_MEMORY:
      if (data.mem.scale == 0) {
        return fmt::format_to(app, "[{} + {}]@{}", get_reg_name(data.mem.base),
                              data.mem.displacement, data.mem.size);
      } else {
        return fmt::format_to(
            app, "[{} + {} + {} * {}]@{}", get_reg_name(data.mem.base),
            data.mem.displacement, get_reg_name(data.mem.index), data.mem.scale,
            data.mem.size);
      }
    case ZYDIS_OPERAND_TYPE_POINTER:
      return fmt::format_to(app, "PTR");
    case ZYDIS_OPERAND_TYPE_IMMEDIATE:
      return fmt::format_to(app, "I{}", data.imm.u);
    }
  }
};

template <>
class fmt::formatter<ZydisEncoderRequest>
    : public BaseIRFormatter<ZydisEncoderRequest> {
public:
  appender format(ZydisEncoderRequest const &data, format_context &ctx) const {
    auto app = ctx.out();
    app = fmt::format_to(app, "{}(", ZydisMnemonicGetString(data.mnemonic));
    for (auto i = 0; i < data.operand_count; i++) {
      app = fmt::format_to(app, "{}, ", data.operands[i]);
    }
    app = fmt::format_to(app, ")");
    return app;
  }
};

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
