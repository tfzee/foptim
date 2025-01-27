#include "third_party/Zydis.h"
#include "utils/logging.hpp"

namespace foptim::codegen {

utils::Printer operator<<(utils::Printer p, const ZydisEncoderOperand &data);
utils::Printer operator<<(utils::Printer p, const ZydisRegister &data);

utils::Printer operator<<(utils::Printer p, const ZydisEncoderOperand &data) {
  switch (data.type) {
  case ZYDIS_OPERAND_TYPE_UNUSED:
    return p << "UNUSED";
  case ZYDIS_OPERAND_TYPE_REGISTER:
    return p << data.reg.value;
  case ZYDIS_OPERAND_TYPE_MEMORY:
    return p << "[" << data.mem.base << " + " << data.mem.displacement << " + "
             << data.mem.index << " * " << (u64)data.mem.scale << "]";
  case ZYDIS_OPERAND_TYPE_POINTER:
    return p << "[O:" << data.ptr.offset << " S:" << data.ptr.segment << "]";
    return p << "ptr";
  case ZYDIS_OPERAND_TYPE_IMMEDIATE:
    return p << "I" << data.imm.u;
  }
}
utils::Printer operator<<(utils::Printer p, const ZydisRegister &data) {
  switch (data) {
  case ZYDIS_REGISTER_NONE:
    return p << "R_NONE";
  case ZYDIS_REGISTER_AL:
    return p << "R_AL";
  case ZYDIS_REGISTER_CL:
    return p << "R_CL";
  case ZYDIS_REGISTER_DL:
    return p << "R_DL";
  case ZYDIS_REGISTER_BL:
    return p << "R_BL";
  case ZYDIS_REGISTER_AH:
    return p << "R_AH";
  case ZYDIS_REGISTER_CH:
    return p << "R_CH";
  case ZYDIS_REGISTER_DH:
    return p << "R_DH";
  case ZYDIS_REGISTER_BH:
    return p << "R_BH";
  case ZYDIS_REGISTER_SPL:
    return p << "R_SPL";
  case ZYDIS_REGISTER_BPL:
    return p << "R_BPL";
  case ZYDIS_REGISTER_SIL:
    return p << "R_SIL";
  case ZYDIS_REGISTER_DIL:
    return p << "R_DIL";
  case ZYDIS_REGISTER_R8B:
    return p << "R_R8B";
  case ZYDIS_REGISTER_R9B:
    return p << "R_R9B";
  case ZYDIS_REGISTER_R10B:
    return p << "R_R10B";
  case ZYDIS_REGISTER_R11B:
    return p << "R_R11B";
  case ZYDIS_REGISTER_R12B:
    return p << "R_R12B";
  case ZYDIS_REGISTER_R13B:
    return p << "R_R13B";
  case ZYDIS_REGISTER_R14B:
    return p << "R_R14B";
  case ZYDIS_REGISTER_R15B:
    return p << "R_R15B";
  case ZYDIS_REGISTER_AX:
    return p << "R_AX";
  case ZYDIS_REGISTER_CX:
    return p << "R_CX";
  case ZYDIS_REGISTER_DX:
    return p << "R_DX";
  case ZYDIS_REGISTER_BX:
    return p << "R_BX";
  case ZYDIS_REGISTER_SP:
    return p << "R_SP";
  case ZYDIS_REGISTER_BP:
    return p << "R_BP";
  case ZYDIS_REGISTER_SI:
    return p << "R_SI";
  case ZYDIS_REGISTER_DI:
    return p << "R_DI";
  case ZYDIS_REGISTER_R8W:
    return p << "R_R8W";
  case ZYDIS_REGISTER_R9W:
    return p << "R_R9W";
  case ZYDIS_REGISTER_R10W:
    return p << "R_R10W";
  case ZYDIS_REGISTER_R11W:
    return p << "R_R11W";
  case ZYDIS_REGISTER_R12W:
    return p << "R_R12W";
  case ZYDIS_REGISTER_R13W:
    return p << "R_R13W";
  case ZYDIS_REGISTER_R14W:
    return p << "R_R14W";
  case ZYDIS_REGISTER_R15W:
    return p << "R_R15W";
  case ZYDIS_REGISTER_EAX:
    return p << "R_EAX";
  case ZYDIS_REGISTER_ECX:
    return p << "R_ECX";
  case ZYDIS_REGISTER_EDX:
    return p << "R_EDX";
  case ZYDIS_REGISTER_EBX:
    return p << "R_EBX";
  case ZYDIS_REGISTER_ESP:
    return p << "R_ESP";
  case ZYDIS_REGISTER_EBP:
    return p << "R_EBP";
  case ZYDIS_REGISTER_ESI:
    return p << "R_ESI";
  case ZYDIS_REGISTER_EDI:
    return p << "R_EDI";
  case ZYDIS_REGISTER_R8D:
    return p << "R_R8D";
  case ZYDIS_REGISTER_R9D:
    return p << "R_R9D";
  case ZYDIS_REGISTER_R10D:
    return p << "R_R10D";
  case ZYDIS_REGISTER_R11D:
    return p << "R_R11D";
  case ZYDIS_REGISTER_R12D:
    return p << "R_R12D";
  case ZYDIS_REGISTER_R13D:
    return p << "R_R13D";
  case ZYDIS_REGISTER_R14D:
    return p << "R_R14D";
  case ZYDIS_REGISTER_R15D:
    return p << "R_R15D";
  case ZYDIS_REGISTER_RAX:
    return p << "R_RAX";
  case ZYDIS_REGISTER_RCX:
    return p << "R_RCX";
  case ZYDIS_REGISTER_RDX:
    return p << "R_RDX";
  case ZYDIS_REGISTER_RBX:
    return p << "R_RBX";
  case ZYDIS_REGISTER_RSP:
    return p << "R_RSP";
  case ZYDIS_REGISTER_RBP:
    return p << "R_RBP";
  case ZYDIS_REGISTER_RSI:
    return p << "R_RSI";
  case ZYDIS_REGISTER_RDI:
    return p << "R_RDI";
  case ZYDIS_REGISTER_R8:
    return p << "R_R8";
  case ZYDIS_REGISTER_R9:
    return p << "R_R9";
  case ZYDIS_REGISTER_R10:
    return p << "R_R10";
  case ZYDIS_REGISTER_R11:
    return p << "R_R11";
  case ZYDIS_REGISTER_R12:
    return p << "R_R12";
  case ZYDIS_REGISTER_R13:
    return p << "R_R13";
  case ZYDIS_REGISTER_R14:
    return p << "R_R14";
  case ZYDIS_REGISTER_R15:
    return p << "R_R15";
  case ZYDIS_REGISTER_ST0:
    return p << "R_ST0";
  case ZYDIS_REGISTER_ST1:
    return p << "R_ST1";
  case ZYDIS_REGISTER_ST2:
    return p << "R_ST2";
  case ZYDIS_REGISTER_ST3:
    return p << "R_ST3";
  case ZYDIS_REGISTER_ST4:
    return p << "R_ST4";
  case ZYDIS_REGISTER_ST5:
    return p << "R_ST5";
  case ZYDIS_REGISTER_ST6:
    return p << "R_ST6";
  case ZYDIS_REGISTER_ST7:
    return p << "R_ST7";
  case ZYDIS_REGISTER_X87CONTROL:
    return p << "R_X87CONTROL";
  case ZYDIS_REGISTER_X87STATUS:
    return p << "R_X87STATUS";
  case ZYDIS_REGISTER_X87TAG:
    return p << "R_X87TAG";
  case ZYDIS_REGISTER_MM0:
    return p << "R_MM0";
  case ZYDIS_REGISTER_MM1:
    return p << "R_MM1";
  case ZYDIS_REGISTER_MM2:
    return p << "R_MM2";
  case ZYDIS_REGISTER_MM3:
    return p << "R_MM3";
  case ZYDIS_REGISTER_MM4:
    return p << "R_MM4";
  case ZYDIS_REGISTER_MM5:
    return p << "R_MM5";
  case ZYDIS_REGISTER_MM6:
    return p << "R_MM6";
  case ZYDIS_REGISTER_MM7:
    return p << "R_MM7";
  case ZYDIS_REGISTER_XMM0:
    return p << "R_XMM0";
  case ZYDIS_REGISTER_XMM1:
    return p << "R_XMM1";
  case ZYDIS_REGISTER_XMM2:
    return p << "R_XMM2";
  case ZYDIS_REGISTER_XMM3:
    return p << "R_XMM3";
  case ZYDIS_REGISTER_XMM4:
    return p << "R_XMM4";
  case ZYDIS_REGISTER_XMM5:
    return p << "R_XMM5";
  case ZYDIS_REGISTER_XMM6:
    return p << "R_XMM6";
  case ZYDIS_REGISTER_XMM7:
    return p << "R_XMM7";
  case ZYDIS_REGISTER_XMM8:
    return p << "R_XMM8";
  case ZYDIS_REGISTER_XMM9:
    return p << "R_XMM9";
  case ZYDIS_REGISTER_XMM10:
    return p << "R_XMM10";
  case ZYDIS_REGISTER_XMM11:
    return p << "R_XMM11";
  case ZYDIS_REGISTER_XMM12:
    return p << "R_XMM12";
  case ZYDIS_REGISTER_XMM13:
    return p << "R_XMM13";
  case ZYDIS_REGISTER_XMM14:
    return p << "R_XMM14";
  case ZYDIS_REGISTER_XMM15:
    return p << "R_XMM15";
  case ZYDIS_REGISTER_XMM16:
    return p << "R_XMM16";
  case ZYDIS_REGISTER_XMM17:
    return p << "R_XMM17";
  case ZYDIS_REGISTER_XMM18:
    return p << "R_XMM18";
  case ZYDIS_REGISTER_XMM19:
    return p << "R_XMM19";
  case ZYDIS_REGISTER_XMM20:
    return p << "R_XMM20";
  case ZYDIS_REGISTER_XMM21:
    return p << "R_XMM21";
  case ZYDIS_REGISTER_XMM22:
    return p << "R_XMM22";
  case ZYDIS_REGISTER_XMM23:
    return p << "R_XMM23";
  case ZYDIS_REGISTER_XMM24:
    return p << "R_XMM24";
  case ZYDIS_REGISTER_XMM25:
    return p << "R_XMM25";
  case ZYDIS_REGISTER_XMM26:
    return p << "R_XMM26";
  case ZYDIS_REGISTER_XMM27:
    return p << "R_XMM27";
  case ZYDIS_REGISTER_XMM28:
    return p << "R_XMM28";
  case ZYDIS_REGISTER_XMM29:
    return p << "R_XMM29";
  case ZYDIS_REGISTER_XMM30:
    return p << "R_XMM30";
  case ZYDIS_REGISTER_XMM31:
    return p << "R_XMM31";
  case ZYDIS_REGISTER_YMM0:
    return p << "R_YMM0";
  case ZYDIS_REGISTER_YMM1:
    return p << "R_YMM1";
  case ZYDIS_REGISTER_YMM2:
    return p << "R_YMM2";
  case ZYDIS_REGISTER_YMM3:
    return p << "R_YMM3";
  case ZYDIS_REGISTER_YMM4:
    return p << "R_YMM4";
  case ZYDIS_REGISTER_YMM5:
    return p << "R_YMM5";
  case ZYDIS_REGISTER_YMM6:
    return p << "R_YMM6";
  case ZYDIS_REGISTER_YMM7:
    return p << "R_YMM7";
  case ZYDIS_REGISTER_YMM8:
    return p << "R_YMM8";
  case ZYDIS_REGISTER_YMM9:
    return p << "R_YMM9";
  case ZYDIS_REGISTER_YMM10:
    return p << "R_YMM10";
  case ZYDIS_REGISTER_YMM11:
    return p << "R_YMM11";
  case ZYDIS_REGISTER_YMM12:
    return p << "R_YMM12";
  case ZYDIS_REGISTER_YMM13:
    return p << "R_YMM13";
  case ZYDIS_REGISTER_YMM14:
    return p << "R_YMM14";
  case ZYDIS_REGISTER_YMM15:
    return p << "R_YMM15";
  case ZYDIS_REGISTER_YMM16:
    return p << "R_YMM16";
  case ZYDIS_REGISTER_YMM17:
    return p << "R_YMM17";
  case ZYDIS_REGISTER_YMM18:
    return p << "R_YMM18";
  case ZYDIS_REGISTER_YMM19:
    return p << "R_YMM19";
  case ZYDIS_REGISTER_YMM20:
    return p << "R_YMM20";
  case ZYDIS_REGISTER_YMM21:
    return p << "R_YMM21";
  case ZYDIS_REGISTER_YMM22:
    return p << "R_YMM22";
  case ZYDIS_REGISTER_YMM23:
    return p << "R_YMM23";
  case ZYDIS_REGISTER_YMM24:
    return p << "R_YMM24";
  case ZYDIS_REGISTER_YMM25:
    return p << "R_YMM25";
  case ZYDIS_REGISTER_YMM26:
    return p << "R_YMM26";
  case ZYDIS_REGISTER_YMM27:
    return p << "R_YMM27";
  case ZYDIS_REGISTER_YMM28:
    return p << "R_YMM28";
  case ZYDIS_REGISTER_YMM29:
    return p << "R_YMM29";
  case ZYDIS_REGISTER_YMM30:
    return p << "R_YMM30";
  case ZYDIS_REGISTER_YMM31:
    return p << "R_YMM31";
  case ZYDIS_REGISTER_ZMM0:
    return p << "R_ZMM0";
  case ZYDIS_REGISTER_ZMM1:
    return p << "R_ZMM1";
  case ZYDIS_REGISTER_ZMM2:
    return p << "R_ZMM2";
  case ZYDIS_REGISTER_ZMM3:
    return p << "R_ZMM3";
  case ZYDIS_REGISTER_ZMM4:
    return p << "R_ZMM4";
  case ZYDIS_REGISTER_ZMM5:
    return p << "R_ZMM5";
  case ZYDIS_REGISTER_ZMM6:
    return p << "R_ZMM6";
  case ZYDIS_REGISTER_ZMM7:
    return p << "R_ZMM7";
  case ZYDIS_REGISTER_ZMM8:
    return p << "R_ZMM8";
  case ZYDIS_REGISTER_ZMM9:
    return p << "R_ZMM9";
  case ZYDIS_REGISTER_ZMM10:
    return p << "R_ZMM10";
  case ZYDIS_REGISTER_ZMM11:
    return p << "R_ZMM11";
  case ZYDIS_REGISTER_ZMM12:
    return p << "R_ZMM12";
  case ZYDIS_REGISTER_ZMM13:
    return p << "R_ZMM13";
  case ZYDIS_REGISTER_ZMM14:
    return p << "R_ZMM14";
  case ZYDIS_REGISTER_ZMM15:
    return p << "R_ZMM15";
  case ZYDIS_REGISTER_ZMM16:
    return p << "R_ZMM16";
  case ZYDIS_REGISTER_ZMM17:
    return p << "R_ZMM17";
  case ZYDIS_REGISTER_ZMM18:
    return p << "R_ZMM18";
  case ZYDIS_REGISTER_ZMM19:
    return p << "R_ZMM19";
  case ZYDIS_REGISTER_ZMM20:
    return p << "R_ZMM20";
  case ZYDIS_REGISTER_ZMM21:
    return p << "R_ZMM21";
  case ZYDIS_REGISTER_ZMM22:
    return p << "R_ZMM22";
  case ZYDIS_REGISTER_ZMM23:
    return p << "R_ZMM23";
  case ZYDIS_REGISTER_ZMM24:
    return p << "R_ZMM24";
  case ZYDIS_REGISTER_ZMM25:
    return p << "R_ZMM25";
  case ZYDIS_REGISTER_ZMM26:
    return p << "R_ZMM26";
  case ZYDIS_REGISTER_ZMM27:
    return p << "R_ZMM27";
  case ZYDIS_REGISTER_ZMM28:
    return p << "R_ZMM28";
  case ZYDIS_REGISTER_ZMM29:
    return p << "R_ZMM29";
  case ZYDIS_REGISTER_ZMM30:
    return p << "R_ZMM30";
  case ZYDIS_REGISTER_ZMM31:
    return p << "R_ZMM31";
  case ZYDIS_REGISTER_TMM0:
    return p << "R_TMM0";
  case ZYDIS_REGISTER_TMM1:
    return p << "R_TMM1";
  case ZYDIS_REGISTER_TMM2:
    return p << "R_TMM2";
  case ZYDIS_REGISTER_TMM3:
    return p << "R_TMM3";
  case ZYDIS_REGISTER_TMM4:
    return p << "R_TMM4";
  case ZYDIS_REGISTER_TMM5:
    return p << "R_TMM5";
  case ZYDIS_REGISTER_TMM6:
    return p << "R_TMM6";
  case ZYDIS_REGISTER_TMM7:
    return p << "R_TMM7";
  case ZYDIS_REGISTER_FLAGS:
    return p << "R_FLAGS";
  case ZYDIS_REGISTER_EFLAGS:
    return p << "R_EFLAGS";
  case ZYDIS_REGISTER_RFLAGS:
    return p << "R_RFLAGS";
  case ZYDIS_REGISTER_IP:
    return p << "R_IP";
  case ZYDIS_REGISTER_EIP:
    return p << "R_EIP";
  case ZYDIS_REGISTER_RIP:
    return p << "R_RIP";
  case ZYDIS_REGISTER_ES:
    return p << "R_ES";
  case ZYDIS_REGISTER_CS:
    return p << "R_CS";
  case ZYDIS_REGISTER_SS:
    return p << "R_SS";
  case ZYDIS_REGISTER_DS:
    return p << "R_DS";
  case ZYDIS_REGISTER_FS:
    return p << "R_FS";
  case ZYDIS_REGISTER_GS:
    return p << "R_GS";
  case ZYDIS_REGISTER_GDTR:
    return p << "R_GDTR";
  case ZYDIS_REGISTER_LDTR:
    return p << "R_LDTR";
  case ZYDIS_REGISTER_IDTR:
    return p << "R_IDTR";
  case ZYDIS_REGISTER_TR:
    return p << "R_TR";
  case ZYDIS_REGISTER_TR0:
    return p << "R_TR0";
  case ZYDIS_REGISTER_TR1:
    return p << "R_TR1";
  case ZYDIS_REGISTER_TR2:
    return p << "R_TR2";
  case ZYDIS_REGISTER_TR3:
    return p << "R_TR3";
  case ZYDIS_REGISTER_TR4:
    return p << "R_TR4";
  case ZYDIS_REGISTER_TR5:
    return p << "R_TR5";
  case ZYDIS_REGISTER_TR6:
    return p << "R_TR6";
  case ZYDIS_REGISTER_TR7:
    return p << "R_TR7";
  case ZYDIS_REGISTER_CR0:
    return p << "R_CR0";
  case ZYDIS_REGISTER_CR1:
    return p << "R_CR1";
  case ZYDIS_REGISTER_CR2:
    return p << "R_CR2";
  case ZYDIS_REGISTER_CR3:
    return p << "R_CR3";
  case ZYDIS_REGISTER_CR4:
    return p << "R_CR4";
  case ZYDIS_REGISTER_CR5:
    return p << "R_CR5";
  case ZYDIS_REGISTER_CR6:
    return p << "R_CR6";
  case ZYDIS_REGISTER_CR7:
    return p << "R_CR7";
  case ZYDIS_REGISTER_CR8:
    return p << "R_CR8";
  case ZYDIS_REGISTER_CR9:
    return p << "R_CR9";
  case ZYDIS_REGISTER_CR10:
    return p << "R_CR10";
  case ZYDIS_REGISTER_CR11:
    return p << "R_CR11";
  case ZYDIS_REGISTER_CR12:
    return p << "R_CR12";
  case ZYDIS_REGISTER_CR13:
    return p << "R_CR13";
  case ZYDIS_REGISTER_CR14:
    return p << "R_CR14";
  case ZYDIS_REGISTER_CR15:
    return p << "R_CR15";
  case ZYDIS_REGISTER_DR0:
    return p << "R_DR0";
  case ZYDIS_REGISTER_DR1:
    return p << "R_DR1";
  case ZYDIS_REGISTER_DR2:
    return p << "R_DR2";
  case ZYDIS_REGISTER_DR3:
    return p << "R_DR3";
  case ZYDIS_REGISTER_DR4:
    return p << "R_DR4";
  case ZYDIS_REGISTER_DR5:
    return p << "R_DR5";
  case ZYDIS_REGISTER_DR6:
    return p << "R_DR6";
  case ZYDIS_REGISTER_DR7:
    return p << "R_DR7";
  case ZYDIS_REGISTER_DR8:
    return p << "R_DR8";
  case ZYDIS_REGISTER_DR9:
    return p << "R_DR9";
  case ZYDIS_REGISTER_DR10:
    return p << "R_DR10";
  case ZYDIS_REGISTER_DR11:
    return p << "R_DR11";
  case ZYDIS_REGISTER_DR12:
    return p << "R_DR12";
  case ZYDIS_REGISTER_DR13:
    return p << "R_DR13";
  case ZYDIS_REGISTER_DR14:
    return p << "R_DR14";
  case ZYDIS_REGISTER_DR15:
    return p << "R_DR15";
  case ZYDIS_REGISTER_K0:
    return p << "R_K0";
  case ZYDIS_REGISTER_K1:
    return p << "R_K1";
  case ZYDIS_REGISTER_K2:
    return p << "R_K2";
  case ZYDIS_REGISTER_K3:
    return p << "R_K3";
  case ZYDIS_REGISTER_K4:
    return p << "R_K4";
  case ZYDIS_REGISTER_K5:
    return p << "R_K5";
  case ZYDIS_REGISTER_K6:
    return p << "R_K6";
  case ZYDIS_REGISTER_K7:
    return p << "R_K7";
  case ZYDIS_REGISTER_BND0:
    return p << "R_BND0";
  case ZYDIS_REGISTER_BND1:
    return p << "R_BND1";
  case ZYDIS_REGISTER_BND2:
    return p << "R_BND2";
  case ZYDIS_REGISTER_BND3:
    return p << "R_BND3";
  case ZYDIS_REGISTER_BNDCFG:
    return p << "R_BNDCFG";
  case ZYDIS_REGISTER_BNDSTATUS:
    return p << "R_BNDSTATUS";
  case ZYDIS_REGISTER_MXCSR:
    return p << "R_MXCSR";
  case ZYDIS_REGISTER_PKRU:
    return p << "R_PKRU";
  case ZYDIS_REGISTER_XCR0:
    return p << "R_XCR0";
  case ZYDIS_REGISTER_UIF:
    return p << "R_UIF";
  }
}
} // namespace foptim::codegen
