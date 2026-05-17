#include "third_party/Zydis.h"
#include "utils/logging.hpp"

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
