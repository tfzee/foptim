#pragma once
#include "backend3.hpp"
#include "mir/instr.hpp"
#include "third_party/Zydis.h"
#include "utils/todo.hpp"
#include "utils/types.hpp"

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
          return fmt::format_to(app, "[{} + {}]@{}",
                                get_reg_name(data.mem.base),
                                data.mem.displacement, data.mem.size);
        } else {
          return fmt::format_to(
              app, "[{} + {} + {} * {}]@{}", get_reg_name(data.mem.base),
              data.mem.displacement, get_reg_name(data.mem.index),
              data.mem.scale, data.mem.size);
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
namespace {

ZydisRegister get_reg_sized_gpr(const ZydisRegister *regs, u32 size) {
  switch (size) {
    case 1:
      return regs[0];
    case 2:
      return regs[1];
    case 4:
      return regs[2];
    case 8:
      return regs[3];
    default:
  }
  fmt::println("Got size: {} but only 1,2,4,8 is valid\n", size);
  ASSERT_M(false, "Tried to get invalid reg size");
  std::abort();
}

ZydisRegister get_reg_sized_vec(const ZydisRegister *regs, u32 size) {
  switch (size) {
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
      return regs[0];
    case 32:
      return regs[1];
    case 64:
      return regs[2];
    default:
  }
  fmt::println("Got size: {} but only 1,2,4,8,16,32,64 is valid\n", size);
  ASSERT_M(false, "Tried to get invalid reg size");
  std::abort();
}

ZydisRegister convert_reg(const fmir::VReg &reg) {
  constexpr ZydisRegister a_regs[] = {ZYDIS_REGISTER_AL, ZYDIS_REGISTER_AX,
                                      ZYDIS_REGISTER_EAX, ZYDIS_REGISTER_RAX};
  constexpr ZydisRegister b_regs[] = {ZYDIS_REGISTER_BL, ZYDIS_REGISTER_BX,
                                      ZYDIS_REGISTER_EBX, ZYDIS_REGISTER_RBX};
  constexpr ZydisRegister c_regs[] = {ZYDIS_REGISTER_CL, ZYDIS_REGISTER_CX,
                                      ZYDIS_REGISTER_ECX, ZYDIS_REGISTER_RCX};
  constexpr ZydisRegister d_regs[] = {ZYDIS_REGISTER_DL, ZYDIS_REGISTER_DX,
                                      ZYDIS_REGISTER_EDX, ZYDIS_REGISTER_RDX};
  constexpr ZydisRegister di_regs[] = {ZYDIS_REGISTER_DIL, ZYDIS_REGISTER_DI,
                                       ZYDIS_REGISTER_EDI, ZYDIS_REGISTER_RDI};
  constexpr ZydisRegister si_regs[] = {ZYDIS_REGISTER_SIL, ZYDIS_REGISTER_SI,
                                       ZYDIS_REGISTER_ESI, ZYDIS_REGISTER_RSI};
  constexpr ZydisRegister sp_regs[] = {ZYDIS_REGISTER_SPL, ZYDIS_REGISTER_SP,
                                       ZYDIS_REGISTER_ESP, ZYDIS_REGISTER_RSP};
  constexpr ZydisRegister bp_regs[] = {ZYDIS_REGISTER_BPL, ZYDIS_REGISTER_BP,
                                       ZYDIS_REGISTER_EBP, ZYDIS_REGISTER_RBP};
  constexpr ZydisRegister r8_regs[] = {ZYDIS_REGISTER_R8B, ZYDIS_REGISTER_R8W,
                                       ZYDIS_REGISTER_R8D, ZYDIS_REGISTER_R8};
  constexpr ZydisRegister r9_regs[] = {ZYDIS_REGISTER_R9B, ZYDIS_REGISTER_R9W,
                                       ZYDIS_REGISTER_R9D, ZYDIS_REGISTER_R9};
  constexpr ZydisRegister r10_regs[] = {
      ZYDIS_REGISTER_R10B, ZYDIS_REGISTER_R10W, ZYDIS_REGISTER_R10D,
      ZYDIS_REGISTER_R10};
  constexpr ZydisRegister r11_regs[] = {
      ZYDIS_REGISTER_R11B, ZYDIS_REGISTER_R11W, ZYDIS_REGISTER_R11D,
      ZYDIS_REGISTER_R11};
  constexpr ZydisRegister r12_regs[] = {
      ZYDIS_REGISTER_R12B, ZYDIS_REGISTER_R12W, ZYDIS_REGISTER_R12D,
      ZYDIS_REGISTER_R12};
  constexpr ZydisRegister r13_regs[] = {
      ZYDIS_REGISTER_R13B, ZYDIS_REGISTER_R13W, ZYDIS_REGISTER_R13D,
      ZYDIS_REGISTER_R13};
  constexpr ZydisRegister r14_regs[] = {
      ZYDIS_REGISTER_R14B, ZYDIS_REGISTER_R14W, ZYDIS_REGISTER_R14D,
      ZYDIS_REGISTER_R14};
  constexpr ZydisRegister r15_regs[] = {
      ZYDIS_REGISTER_R15B, ZYDIS_REGISTER_R15W, ZYDIS_REGISTER_R15D,
      ZYDIS_REGISTER_R15};
  constexpr ZydisRegister xmm0_regs[] = {
      ZYDIS_REGISTER_XMM0, ZYDIS_REGISTER_YMM0, ZYDIS_REGISTER_ZMM0};
  constexpr ZydisRegister xmm1_regs[] = {
      ZYDIS_REGISTER_XMM1, ZYDIS_REGISTER_YMM1, ZYDIS_REGISTER_ZMM1};
  constexpr ZydisRegister xmm2_regs[] = {
      ZYDIS_REGISTER_XMM2, ZYDIS_REGISTER_YMM2, ZYDIS_REGISTER_ZMM0};
  constexpr ZydisRegister xmm3_regs[] = {
      ZYDIS_REGISTER_XMM3, ZYDIS_REGISTER_YMM3, ZYDIS_REGISTER_ZMM0};
  constexpr ZydisRegister xmm4_regs[] = {
      ZYDIS_REGISTER_XMM4, ZYDIS_REGISTER_YMM4, ZYDIS_REGISTER_ZMM0};
  constexpr ZydisRegister xmm5_regs[] = {
      ZYDIS_REGISTER_XMM5, ZYDIS_REGISTER_YMM5, ZYDIS_REGISTER_ZMM0};
  constexpr ZydisRegister xmm6_regs[] = {
      ZYDIS_REGISTER_XMM6, ZYDIS_REGISTER_YMM6, ZYDIS_REGISTER_ZMM0};
  constexpr ZydisRegister xmm7_regs[] = {
      ZYDIS_REGISTER_XMM7, ZYDIS_REGISTER_YMM7, ZYDIS_REGISTER_ZMM0};
  constexpr ZydisRegister xmm8_regs[] = {
      ZYDIS_REGISTER_XMM8, ZYDIS_REGISTER_YMM8, ZYDIS_REGISTER_ZMM0};
  constexpr ZydisRegister xmm9_regs[] = {
      ZYDIS_REGISTER_XMM9, ZYDIS_REGISTER_YMM9, ZYDIS_REGISTER_ZMM0};
  constexpr ZydisRegister xmm10_regs[] = {
      ZYDIS_REGISTER_XMM10, ZYDIS_REGISTER_YMM10, ZYDIS_REGISTER_ZMM10};
  constexpr ZydisRegister xmm11_regs[] = {
      ZYDIS_REGISTER_XMM11, ZYDIS_REGISTER_YMM11, ZYDIS_REGISTER_ZMM11};
  constexpr ZydisRegister xmm12_regs[] = {
      ZYDIS_REGISTER_XMM12, ZYDIS_REGISTER_YMM12, ZYDIS_REGISTER_ZMM12};
  constexpr ZydisRegister xmm13_regs[] = {
      ZYDIS_REGISTER_XMM13, ZYDIS_REGISTER_YMM13, ZYDIS_REGISTER_ZMM13};
  constexpr ZydisRegister xmm14_regs[] = {
      ZYDIS_REGISTER_XMM14, ZYDIS_REGISTER_YMM14, ZYDIS_REGISTER_ZMM14};
  constexpr ZydisRegister xmm15_regs[] = {
      ZYDIS_REGISTER_XMM15, ZYDIS_REGISTER_YMM15, ZYDIS_REGISTER_ZMM15};

  switch (reg.c_reg()) {
    case fmir::CReg::A:
      return get_reg_sized_gpr(a_regs, reg.size());
    case fmir::CReg::B:
      return get_reg_sized_gpr(b_regs, reg.size());
    case fmir::CReg::C:
      return get_reg_sized_gpr(c_regs, reg.size());
    case fmir::CReg::D:
      return get_reg_sized_gpr(d_regs, reg.size());
    case fmir::CReg::DI:
      return get_reg_sized_gpr(di_regs, reg.size());
    case fmir::CReg::SI:
      return get_reg_sized_gpr(si_regs, reg.size());
    case fmir::CReg::SP:
      return get_reg_sized_gpr(sp_regs, reg.size());
    case fmir::CReg::BP:
      return get_reg_sized_gpr(bp_regs, reg.size());
    case fmir::CReg::R8:
      return get_reg_sized_gpr(r8_regs, reg.size());
    case fmir::CReg::R9:
      return get_reg_sized_gpr(r9_regs, reg.size());
    case fmir::CReg::R10:
      return get_reg_sized_gpr(r10_regs, reg.size());
    case fmir::CReg::R11:
      return get_reg_sized_gpr(r11_regs, reg.size());
    case fmir::CReg::R12:
      return get_reg_sized_gpr(r12_regs, reg.size());
    case fmir::CReg::R13:
      return get_reg_sized_gpr(r13_regs, reg.size());
    case fmir::CReg::R14:
      return get_reg_sized_gpr(r14_regs, reg.size());
    case fmir::CReg::R15:
      return get_reg_sized_gpr(r15_regs, reg.size());
    case fmir::CReg::mm0:
      return get_reg_sized_vec(xmm0_regs, reg.size());
    case fmir::CReg::mm1:
      return get_reg_sized_vec(xmm1_regs, reg.size());
    case fmir::CReg::mm2:
      return get_reg_sized_vec(xmm2_regs, reg.size());
    case fmir::CReg::mm3:
      return get_reg_sized_vec(xmm3_regs, reg.size());
    case fmir::CReg::mm4:
      return get_reg_sized_vec(xmm4_regs, reg.size());
    case fmir::CReg::mm5:
      return get_reg_sized_vec(xmm5_regs, reg.size());
    case fmir::CReg::mm6:
      return get_reg_sized_vec(xmm6_regs, reg.size());
    case fmir::CReg::mm7:
      return get_reg_sized_vec(xmm7_regs, reg.size());
    case fmir::CReg::mm8:
      return get_reg_sized_vec(xmm8_regs, reg.size());
    case fmir::CReg::mm9:
      return get_reg_sized_vec(xmm9_regs, reg.size());
    case fmir::CReg::mm10:
      return get_reg_sized_vec(xmm10_regs, reg.size());
    case fmir::CReg::mm11:
      return get_reg_sized_vec(xmm11_regs, reg.size());
    case fmir::CReg::mm12:
      return get_reg_sized_vec(xmm12_regs, reg.size());
    case fmir::CReg::mm13:
      return get_reg_sized_vec(xmm13_regs, reg.size());
    case fmir::CReg::mm14:
      return get_reg_sized_vec(xmm14_regs, reg.size());
    case fmir::CReg::mm15:
      return get_reg_sized_vec(xmm15_regs, reg.size());
    case fmir::CReg::N_REGS:
    case fmir::CReg::Virtual:
      UNREACH();
  }
}

ZydisRegister reg_with_type(fmir::VReg reg, fmir::Type new_type) {
  reg.ty = new_type;
  return convert_reg(reg);
}

void emit_operand(const fmir::MArgument &arg, ZydisEncoderOperand &operand,
                  TLabelUsageMap &reloc_map, u8 *instr_ptr, u8 arg_id) {
  switch (arg.type) {
    case fmir::MArgument::ArgumentType::Imm:
      operand.type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
      switch (arg.ty) {
        case fmir::Type::INVALID:
        case fmir::Type::Int8:
        case fmir::Type::Int16:
        case fmir::Type::Int32:
        case fmir::Type::Int64:
          operand.imm.s = std::bit_cast<i64>(arg.imm);
          // fmt::println("{} => {} {}", arg, operand.imm.u, operand.imm.s);
          // operand.imm.s = (i32)arg.imm;
          break;
        case fmir::Type::Float32: {
          f32 v = std::bit_cast<f32>((u32)std::bit_cast<u64>(arg.immf));
          int i;
          memcpy(&i, &v, sizeof i);
          operand.imm.s = i;
          break;
        }
        case fmir::Type::Float64: {
          long int i;
          memcpy(&i, &arg.immf, sizeof i);
          operand.imm.s = i;
          break;
        }
        case fmir::Type::Int32x4:
        case fmir::Type::Int64x2:
        case fmir::Type::Float32x2:
        case fmir::Type::Float32x4:
        case fmir::Type::Float64x2:
        case fmir::Type::Int32x8:
        case fmir::Type::Int64x4:
        case fmir::Type::Float32x8:
        case fmir::Type::Float64x4:
          TODO("invalid");
          break;
      }
      return;
    case fmir::MArgument::ArgumentType::VReg:
      operand.type = ZYDIS_OPERAND_TYPE_REGISTER;
      operand.reg.value = convert_reg(arg.reg);
      // TODO?
      operand.reg.is4 = 0U;
      return;
    case fmir::MArgument::ArgumentType::Label:
      reloc_map.insert_label_ref(arg.label, instr_ptr, arg_id,
                                 RelocSection::Text);
      operand.type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
      operand.imm.s = 0;
      return;
    case fmir::MArgument::ArgumentType::MemLabel:
      reloc_map.insert_label_ref(arg.label, instr_ptr, arg_id,
                                 RelocSection::Text);
      operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
      operand.mem.base = ZYDIS_REGISTER_RIP;
      operand.mem.displacement = 0;
      operand.mem.index = ZYDIS_REGISTER_NONE;
      operand.mem.scale = 0;
      operand.mem.size = get_size(arg.ty);
      return;
    case fmir::MArgument::ArgumentType::MemImmLabel:
      reloc_map.insert_label_ref(arg.label, instr_ptr, arg_id,
                                 RelocSection::Text);
      operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
      operand.mem.base = ZYDIS_REGISTER_RIP;
      operand.mem.displacement = arg.imm;
      operand.mem.index = ZYDIS_REGISTER_NONE;
      operand.mem.scale = 0;
      operand.mem.size = get_size(arg.ty);
      return;
    case fmir::MArgument::ArgumentType::MemVRegVReg:
      operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
      operand.mem.base = convert_reg(arg.reg);
      operand.mem.displacement = 0;
      operand.mem.index = convert_reg(arg.indx);
      operand.mem.scale = 1;
      operand.mem.size = get_size(arg.ty);
      return;
    case fmir::MArgument::ArgumentType::MemVRegVRegScale:
      operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
      operand.mem.base = convert_reg(arg.reg);
      operand.mem.displacement = 0;
      operand.mem.index = convert_reg(arg.indx);
      operand.mem.scale = 1 << arg.scale;
      operand.mem.size = get_size(arg.ty);
      return;
    case fmir::MArgument::ArgumentType::MemVReg:
      operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
      operand.mem.base = convert_reg(arg.reg);
      operand.mem.displacement = 0;
      operand.mem.index = ZYDIS_REGISTER_NONE;
      operand.mem.scale = 0;
      operand.mem.size = get_size(arg.ty);
      return;
    case fmir::MArgument::ArgumentType::MemImmVReg:
      operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
      operand.mem.base = convert_reg(arg.reg);
      operand.mem.displacement = arg.imm;
      operand.mem.index = ZYDIS_REGISTER_NONE;
      operand.mem.scale = 0;
      operand.mem.size = get_size(arg.ty);
      return;
    case fmir::MArgument::ArgumentType::MemImm:
      operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
      operand.mem.base = ZYDIS_REGISTER_NONE;
      operand.mem.displacement = arg.imm;
      operand.mem.index = ZYDIS_REGISTER_NONE;
      operand.mem.scale = 0;
      operand.mem.size = get_size(arg.ty);
      return;
    case fmir::MArgument::ArgumentType::MemImmVRegVReg:
      operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
      operand.mem.base = convert_reg(arg.reg);
      operand.mem.displacement = arg.imm;
      operand.mem.index = convert_reg(arg.indx);
      operand.mem.scale = 1;
      operand.mem.size = get_size(arg.ty);
      return;
    case fmir::MArgument::ArgumentType::MemImmVRegVRegScale:
      operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
      operand.mem.base = convert_reg(arg.reg);
      operand.mem.displacement = arg.imm;
      operand.mem.index = convert_reg(arg.indx);
      operand.mem.scale = 1 << arg.scale;
      operand.mem.size = get_size(arg.ty);
      return;
    case fmir::MArgument::ArgumentType::MemImmVRegScale:
      operand.type = ZYDIS_OPERAND_TYPE_MEMORY;
      operand.mem.base = ZYDIS_REGISTER_NONE;
      operand.mem.displacement = arg.imm;
      operand.mem.index = convert_reg(arg.indx);
      operand.mem.scale = 1 << arg.scale;
      operand.mem.size = get_size(arg.ty);
      return;
  }
}

#define ZY_ASS(status)                                    \
  do {                                                    \
    const ZyanStatus status_047620348 = (status);         \
    if (!ZYAN_SUCCESS(status_047620348)) {                \
      fmt::println("Zyan op failed: {} in module: {}",    \
                   ZYAN_STATUS_CODE(status_047620348),    \
                   ZYAN_STATUS_MODULE(status_047620348)); \
      TODO("");                                           \
    }                                                     \
  } while (0)

#define ZY_ASS_REQ(status, req)                           \
  do {                                                    \
    const ZyanStatus status_047620348 = (status);         \
    if (!ZYAN_SUCCESS(status_047620348)) {                \
      fmt::println("{}", req);                            \
      fmt::println("Zyan op failed: {} in module: {}",    \
                   ZYAN_STATUS_CODE(status_047620348),    \
                   ZYAN_STATUS_MODULE(status_047620348)); \
      TODO("");                                           \
    }                                                     \
  } while (0)
// fmt::println("With req: {}", req);

#define emit(buff, off, req) emit_impl(buff, off, req, __LINE__)

u64 emit_impl(u8 *buff, u32 curr_off, ZydisEncoderRequest *req, int line) {
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

size_t emit_move(const fmir::MInstr &instr, ZydisEncoderRequest &req,
                 u8 *const out_buff) {
  bool input_is_fp_reg =
      instr.args[1].isReg() && instr.args[1].reg.is_vec_reg();
  bool target_is_fp_reg =
      instr.args[0].isReg() && instr.args[0].reg.is_vec_reg();
  bool target_isfloat64 = target_is_fp_reg && instr.args[0].reg.size() == 8;
  bool target_isfloat32 = target_is_fp_reg && instr.args[0].reg.size() == 4;
  bool target_is_vec =
      target_is_fp_reg && instr.args[0].ty > fmir::Type::Float64;
  bool input_isfloat64 = input_is_fp_reg && instr.args[1].reg.size() == 8;
  bool input_isfloat32 = input_is_fp_reg && instr.args[1].reg.size() == 4;
  bool input_is_vec = input_is_fp_reg && instr.args[1].ty > fmir::Type::Float64;
  req.mnemonic = ZYDIS_MNEMONIC_MOV;

  if ((input_is_vec && target_is_vec) ||
      (target_is_vec && instr.args[1].isMem()) ||
      (input_is_vec && instr.args[0].isMem())) {
    auto arg_index = input_is_vec ? 1 : 0;
    switch (instr.args[arg_index].ty) {
        // TODO: aligned??
      case fmir::Type::Float32x2:
        req.mnemonic = target_is_fp_reg && input_is_fp_reg
                           ? ZYDIS_MNEMONIC_MOVUPS
                           : ZYDIS_MNEMONIC_MOVQ;
        break;
      case fmir::Type::Float32x4:
      case fmir::Type::Int32x4:
      case fmir::Type::Float32x8:
      case fmir::Type::Int32x8:
        req.mnemonic = ZYDIS_MNEMONIC_MOVUPS;
        break;
      case fmir::Type::Int64x2:
      case fmir::Type::Float64x2:
      case fmir::Type::Int64x4:
      case fmir::Type::Float64x4:
        req.mnemonic = ZYDIS_MNEMONIC_VMOVUPD;
        break;
      default:
        TODO("UNREACH?");
    }
  } else if (instr.args[1].isMem() && target_isfloat64) {
    req.mnemonic = ZYDIS_MNEMONIC_VMOVSD;
  } else if (instr.args[1].isMem() && target_isfloat32) {
    req.mnemonic = ZYDIS_MNEMONIC_VMOVSS;
  } else if ((!input_is_fp_reg && target_isfloat32) ||
             (!target_is_fp_reg && input_isfloat32)) {
    req.mnemonic = ZYDIS_MNEMONIC_MOVD;
  } else if ((!input_is_fp_reg && target_isfloat64) ||
             (!target_is_fp_reg && input_isfloat64)) {
    if (instr.args[1].isReg() && instr.args[1].reg.size() < 8) {
      req.operands[1].reg.value =
          reg_with_type(instr.args[1].reg, fmir::Type::Int64);
    }
    req.mnemonic = ZYDIS_MNEMONIC_MOVQ;
  } else if (target_isfloat32) {
    req.mnemonic = ZYDIS_MNEMONIC_MOVAPS;
  } else if (target_isfloat64) {
    req.mnemonic = ZYDIS_MNEMONIC_MOVAPD;
  } else if (instr.args[0].isReg() && instr.args[1].isReg() &&
             instr.args[0].reg.size() == 8 && instr.args[1].reg.size() == 4) {
    req.operands[1].reg.value =
        reg_with_type(instr.args[1].reg, fmir::Type::Int64);
  } else if (instr.args[0].isReg() && instr.args[1].isReg() &&
             instr.args[0].reg.size() == 2 && instr.args[1].reg.size() > 2) {
    req.operands[1].reg.value =
        reg_with_type(instr.args[1].reg, fmir::Type::Int16);
  } else if (instr.args[0].isReg() && instr.args[1].isReg() &&
             instr.args[0].reg.size() == 1 && instr.args[1].reg.size() > 1) {
    req.operands[1].reg.value =
        reg_with_type(instr.args[1].reg, fmir::Type::Int8);
  }
  return emit(out_buff, 0, &req);
}

size_t emit_div(const fmir::MInstr &instr, ZydisEncoderRequest &req,
                u8 *const out_buff) {
  size_t length = 9999;
  req.mnemonic = ZYDIS_MNEMONIC_IDIV;
  req.operand_count = 1;

  bool collision_in_reg = req.operands[3].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                          (req.operands[3].reg.value == ZYDIS_REGISTER_DL ||
                           req.operands[3].reg.value == ZYDIS_REGISTER_DH ||
                           req.operands[3].reg.value == ZYDIS_REGISTER_DX ||
                           req.operands[3].reg.value == ZYDIS_REGISTER_EDX ||
                           req.operands[3].reg.value == ZYDIS_REGISTER_RDX);

  size_t out_len = 0;
  // since edx needs to be free for expanding the eax value we will push it
  // onto the stack and use a mem operand
  if (collision_in_reg) {
    size_t len2 = 9999;
    req.mnemonic = ZYDIS_MNEMONIC_MOV;
    req.operand_count = 2;
    req.operands[0].type = ZYDIS_OPERAND_TYPE_MEMORY;
    req.operands[0].mem.base = ZYDIS_REGISTER_RSP;
    req.operands[0].mem.displacement = -8;
    req.operands[0].mem.index = ZYDIS_REGISTER_NONE;
    req.operands[0].mem.scale = 0;
    req.operands[0].mem.size = get_size(instr.args[3].ty);
    req.operands[1] = req.operands[3];
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff + out_len, &len2));
    out_len += len2;
  }
  bool is_udiv = instr.is(fmir::GArithSubtype::udiv);
  if (is_udiv) {
    size_t len2 = 9999;
    req.mnemonic = ZYDIS_MNEMONIC_XOR;
    req.operand_count = 2;
    req.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
    req.operands[0].reg.value = ZYDIS_REGISTER_RDX;
    req.operands[1].type = ZYDIS_OPERAND_TYPE_REGISTER;
    req.operands[1].reg.value = ZYDIS_REGISTER_RDX;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff + out_len, &len2));
    out_len += len2;
  } else if (req.operands[2].reg.value == ZYDIS_REGISTER_RAX) {
    // need to sign extend rax into rdx
    size_t len2 = 9999;
    req.mnemonic = ZYDIS_MNEMONIC_CQO;
    req.operand_count = 0;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff + out_len, &len2));
    out_len += len2;
  } else if (req.operands[2].reg.value == ZYDIS_REGISTER_EAX) {
    // need to sign extend eax into edx
    size_t len2 = 9999;
    req.mnemonic = ZYDIS_MNEMONIC_CDQ;
    req.operand_count = 0;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff + out_len, &len2));
    out_len += len2;
  } else if (req.operands[2].reg.value == ZYDIS_REGISTER_AX) {
    // need to sign extend ax into dx
    size_t len2 = 9999;
    req.mnemonic = ZYDIS_MNEMONIC_CWD;
    req.operand_count = 0;
    ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff + out_len, &len2));
    out_len += len2;
  } else {
    TODO("");
  }

  if (collision_in_reg) {
    req.mnemonic = is_udiv ? ZYDIS_MNEMONIC_DIV : ZYDIS_MNEMONIC_IDIV;
    req.operand_count = 1;
    req.operands[0].type = ZYDIS_OPERAND_TYPE_MEMORY;
    req.operands[0].mem.base = ZYDIS_REGISTER_RSP;
    req.operands[0].mem.displacement = -8;
    req.operands[0].mem.index = ZYDIS_REGISTER_NONE;
    req.operands[0].mem.scale = 0;
    req.operands[0].mem.size = get_size(instr.args[3].ty);
    ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff + out_len, &length),
               req);
  } else {
    req.mnemonic = is_udiv ? ZYDIS_MNEMONIC_DIV : ZYDIS_MNEMONIC_IDIV;
    req.operand_count = 1;
    req.operands[0] = req.operands[3];
    ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff + out_len, &length),
               req);
  }
  return length + out_len;
}

size_t emit_gbase(ZydisEncoderRequest &req, const fmir::MInstr &instr,
                  u8 *const out_buff, u8 curr_bb_id, TLabelUsageMap &reloc_map,
                  ProEpilogueType proepiloguetype) {
  (void)req;
  (void)instr;
  (void)out_buff;
  (void)curr_bb_id;
  (void)reloc_map;
  (void)proepiloguetype;
  // size_t length = 9999;
  switch ((fmir::GBaseSubtype)instr.sop) {
    case fmir::GBaseSubtype::push: {
      assert(req.operand_count == 1);
      if (instr.args[0].isReg() && instr.args[0].reg.is_vec_reg()) {
        u64 off = 0;
        auto real_arg = req.operands[0];
        auto size = get_size(instr.args[0].ty);
        ASSERT(size == 4 || size == 8 || size == 32);
        req.mnemonic = ZYDIS_MNEMONIC_SUB;
        req.operand_count = 2;
        req.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
        req.operands[0].reg.value = ZYDIS_REGISTER_RSP;
        req.operands[1].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
        req.operands[1].imm.u = std::max(size, 8U);
        off = emit(out_buff, off, &req);

        if (size == 32) {
          req.mnemonic = ZYDIS_MNEMONIC_VMOVUPD;
        } else if (size == 8) {
          req.mnemonic = ZYDIS_MNEMONIC_MOVSD;
        } else if (size == 4) {
          req.mnemonic = ZYDIS_MNEMONIC_MOVSS;
        } else {
          TODO("okak");
        }
        req.operands[0].type = ZYDIS_OPERAND_TYPE_MEMORY;
        req.operands[0].mem.base = ZYDIS_REGISTER_RSP;
        req.operands[0].mem.size = size;
        req.operands[1] = real_arg;
        return emit(out_buff, off, &req);
      }
      req.mnemonic = ZYDIS_MNEMONIC_PUSH;
      return emit(out_buff, 0, &req);
    }
    case fmir::GBaseSubtype::mov:
      return emit_move(instr, req, out_buff);
    case fmir::GBaseSubtype::call:
      req.mnemonic = ZYDIS_MNEMONIC_CALL;
      return emit(out_buff, 0, &req);
    case fmir::GBaseSubtype::ret: {
      u64 off = 0;
      // TODO: kinda sus
      ZydisEncoderRequest req;
      memset(&req, 0, sizeof(req));
      if (proepiloguetype == ProEpilogueType::Full) {
        req.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
        req.operands[1].type = ZYDIS_OPERAND_TYPE_REGISTER;

        req.mnemonic = ZYDIS_MNEMONIC_MOV;
        req.operand_count = 2;
        req.operands[0].reg.value = ZYDIS_REGISTER_RSP;
        req.operands[1].reg.value = ZYDIS_REGISTER_RBP;
        off = emit(out_buff, off, &req);

        req.mnemonic = ZYDIS_MNEMONIC_POP;
        req.operand_count = 1;
        req.operands[0].reg.value = ZYDIS_REGISTER_RBP;
        off = emit(out_buff, off, &req);
      } else if (proepiloguetype == ProEpilogueType::Align) {
        req.mnemonic = ZYDIS_MNEMONIC_POP;
        req.operand_count = 1;
        req.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
        req.operands[0].reg.value = ZYDIS_REGISTER_RBP;
        off = emit(out_buff, off, &req);
      }
      req.mnemonic = ZYDIS_MNEMONIC_RET;
      req.operand_count = 0;
      return emit(out_buff, off, &req);
    }
    case fmir::GBaseSubtype::pop:
      assert(req.operand_count == 1);
      if (instr.args[0].isReg() && instr.args[0].reg.is_vec_reg()) {
        u64 off = 0;
        auto real_arg = req.operands[0];
        auto size = get_size(instr.args[0].ty);
        ASSERT(size == 4 || size == 8 || size == 32)

        if (size == 32) {
          req.mnemonic = ZYDIS_MNEMONIC_VMOVUPD;
        } else if (size == 8) {
          req.mnemonic = ZYDIS_MNEMONIC_MOVSD;
        } else if (size == 4) {
          req.mnemonic = ZYDIS_MNEMONIC_MOVSS;
        } else {
          TODO("oka");
        }
        req.operand_count = 2;
        req.operands[0] = real_arg;
        req.operands[1].type = ZYDIS_OPERAND_TYPE_MEMORY;
        req.operands[1].mem.base = ZYDIS_REGISTER_RSP;
        req.operands[1].mem.size = size;
        off = emit(out_buff, off, &req);

        req.mnemonic = ZYDIS_MNEMONIC_ADD;
        req.operands[0].type = ZYDIS_OPERAND_TYPE_REGISTER;
        req.operands[0].reg.value = ZYDIS_REGISTER_RSP;
        req.operands[1].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
        req.operands[1].imm.u = std::max(size, 8U);
        return emit(out_buff, off, &req);
      }
      req.mnemonic = ZYDIS_MNEMONIC_POP;
      return emit(out_buff, 0, &req);
    case fmir::GBaseSubtype::arg_setup:
    case fmir::GBaseSubtype::invoke:
    case fmir::GBaseSubtype::INVALID:
      fmt::println("{:cd}", instr);
      TODO("impl");
  }
}

size_t emit_gjmp(ZydisEncoderRequest &req, const fmir::MInstr &instr,
                 u8 *const out_buff, u8 curr_bb_id, TLabelUsageMap &reloc_map,
                 ProEpilogueType proepiloguetype) {
  (void)req;
  (void)instr;
  (void)out_buff;
  (void)curr_bb_id;
  (void)reloc_map;
  (void)proepiloguetype;
  size_t length = 9999;
  switch ((fmir::GJumpSubtype)instr.sop) {
    case fmir::GJumpSubtype::icmp_slt:
    case fmir::GJumpSubtype::icmp_eq:
    case fmir::GJumpSubtype::icmp_ult:
    case fmir::GJumpSubtype::icmp_ne:
    case fmir::GJumpSubtype::icmp_sgt:
    case fmir::GJumpSubtype::icmp_ugt:
    case fmir::GJumpSubtype::icmp_uge:
    case fmir::GJumpSubtype::icmp_ule:
    case fmir::GJumpSubtype::icmp_sge:
    case fmir::GJumpSubtype::icmp_sle: {
      auto targ = req.operands[0];
      auto a = req.operands[1];
      auto b = req.operands[2];
      req.mnemonic = ZYDIS_MNEMONIC_CMP;
      req.operands[0] = a;
      req.operands[1] = b;
      req.operand_count = 2;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      size_t len2 = 32;
      req.mnemonic = ZYDIS_MNEMONIC_INVALID;

      switch ((fmir::GJumpSubtype)instr.sop) {
        case fmir::GJumpSubtype::icmp_slt:
          req.mnemonic = ZYDIS_MNEMONIC_SETL;
          break;
        case fmir::GJumpSubtype::icmp_ult:
          req.mnemonic = ZYDIS_MNEMONIC_SETB;
          break;
        case fmir::GJumpSubtype::icmp_ne:
          req.mnemonic = ZYDIS_MNEMONIC_SETNZ;
          break;
        case fmir::GJumpSubtype::icmp_uge:
          req.mnemonic = ZYDIS_MNEMONIC_SETNB;
          break;
        case fmir::GJumpSubtype::icmp_ule:
          req.mnemonic = ZYDIS_MNEMONIC_SETBE;
          break;
        case fmir::GJumpSubtype::icmp_sgt:
          req.mnemonic = ZYDIS_MNEMONIC_SETNLE;
          break;
        case fmir::GJumpSubtype::icmp_sge:
          req.mnemonic = ZYDIS_MNEMONIC_SETNL;
          break;
        case fmir::GJumpSubtype::icmp_sle:
          req.mnemonic = ZYDIS_MNEMONIC_SETLE;
          break;
        case fmir::GJumpSubtype::icmp_ugt:
          req.mnemonic = ZYDIS_MNEMONIC_SETNBE;
          break;
        case fmir::GJumpSubtype::icmp_eq:
          req.mnemonic = ZYDIS_MNEMONIC_SETZ;
          break;
        default:
          UNREACH();
      }
      req.operands[0] = targ;
      req.operand_count = 1;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff + length, &len2),
                 req);
      return length + len2;
    }
    case fmir::GJumpSubtype::cjmp_int_slt:
    case fmir::GJumpSubtype::cjmp_int_sge:
    case fmir::GJumpSubtype::cjmp_int_sle:
    case fmir::GJumpSubtype::cjmp_int_sgt:
    case fmir::GJumpSubtype::cjmp_int_ult:
    case fmir::GJumpSubtype::cjmp_int_ule:
    case fmir::GJumpSubtype::cjmp_int_ugt:
    case fmir::GJumpSubtype::cjmp_int_uge:
    case fmir::GJumpSubtype::cjmp_int_ne:
    case fmir::GJumpSubtype::cjmp_int_eq: {
      if ((instr.is(fmir::GJumpSubtype::cjmp_int_eq) ||
           instr.is(fmir::GJumpSubtype::cjmp_int_ne)) &&
          (req.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE &&
           req.operands[1].imm.s == 0)) {
        // Assembly/Compiler Coding Rule 18. (M impact, ML generality)
        // Software
        // can enable macro fusion when it can be logically determined that a
        // variable is non-negative at the time of comparison; use TEST
        // appropriately to enable macrofusion when comparing a variable with 0.
        req.mnemonic = ZYDIS_MNEMONIC_TEST;
        req.operands[1] = req.operands[0];
        ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      } else {
        req.mnemonic = ZYDIS_MNEMONIC_CMP;
        ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      }
      size_t len2 = 999;
      switch ((fmir::GJumpSubtype)instr.sop) {
        case fmir::GJumpSubtype::cjmp_int_slt:
          req.mnemonic = ZYDIS_MNEMONIC_JL;
          break;
        case fmir::GJumpSubtype::cjmp_int_ule:
          req.mnemonic = ZYDIS_MNEMONIC_JBE;
          break;
        case fmir::GJumpSubtype::cjmp_int_sge:
          req.mnemonic = ZYDIS_MNEMONIC_JNL;
          break;
        case fmir::GJumpSubtype::cjmp_int_sle:
          req.mnemonic = ZYDIS_MNEMONIC_JLE;
          break;
        case fmir::GJumpSubtype::cjmp_int_sgt:
          req.mnemonic = ZYDIS_MNEMONIC_JNLE;
          break;
        case fmir::GJumpSubtype::cjmp_int_ult:
          req.mnemonic = ZYDIS_MNEMONIC_JB;
          break;
        case fmir::GJumpSubtype::cjmp_int_ugt:
          req.mnemonic = ZYDIS_MNEMONIC_JNBE;
          break;
        case fmir::GJumpSubtype::cjmp_int_uge:
          req.mnemonic = ZYDIS_MNEMONIC_JNB;
          break;
        case fmir::GJumpSubtype::cjmp_int_ne:
          req.mnemonic = ZYDIS_MNEMONIC_JNZ;
          break;
        case fmir::GJumpSubtype::cjmp_int_eq:
          req.mnemonic = ZYDIS_MNEMONIC_JZ;
          break;
        default:
          UNREACH();
      }
      req.branch_type = ZYDIS_BRANCH_TYPE_NEAR;
      req.operands[0].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
      req.operands[0].imm.s = 0;
      req.operand_count = 1;
      ASSERT(instr.has_bb_ref);
      reloc_map.insert_bb_ref(instr.bb_ref, out_buff + length, 0,
                              RelocSection::Text);
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff + length, &len2),
                 req);
      return length + len2;
    }
    case fmir::GJumpSubtype::cjmp: {
      req.mnemonic = ZYDIS_MNEMONIC_TEST;
      req.operands[1] = req.operands[0];
      req.operand_count = 2;
      u64 off = 0;
      off = emit(out_buff, off, &req);

      ASSERT(instr.has_bb_ref);
      req.mnemonic = ZYDIS_MNEMONIC_JNZ;
      req.branch_type = ZYDIS_BRANCH_TYPE_NEAR;
      req.operands[0].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
      req.operands[0].imm.s = 0;
      req.operand_count = 1;
      reloc_map.insert_bb_ref(instr.bb_ref, out_buff + off, 0,
                              RelocSection::Text);
      return emit(out_buff, off, &req);
    }
    case fmir::GJumpSubtype::jmp:
      req.mnemonic = ZYDIS_MNEMONIC_JMP;
      req.branch_type = ZYDIS_BRANCH_TYPE_NEAR;
      req.operands[0].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
      req.operands[0].imm.s = 0;
      req.operand_count = 1;
      ASSERT(instr.has_bb_ref);
      if (instr.bb_ref == curr_bb_id + 1) {
        return 0;
      }
      reloc_map.insert_bb_ref(instr.bb_ref, out_buff, 0, RelocSection::Text);
      return emit(out_buff, 0, &req);
    case fmir::GJumpSubtype::fcmp_isNaN:
    case fmir::GJumpSubtype::fcmp_ogt:
    case fmir::GJumpSubtype::fcmp_oeq:
    case fmir::GJumpSubtype::fcmp_oge:
    case fmir::GJumpSubtype::fcmp_olt:
    case fmir::GJumpSubtype::fcmp_ole:
    case fmir::GJumpSubtype::fcmp_one:
    case fmir::GJumpSubtype::fcmp_ord:
    case fmir::GJumpSubtype::fcmp_uno:
    case fmir::GJumpSubtype::fcmp_ueq:
    case fmir::GJumpSubtype::fcmp_ugt:
    case fmir::GJumpSubtype::fcmp_uge:
    case fmir::GJumpSubtype::fcmp_ult:
    case fmir::GJumpSubtype::fcmp_ule:
    case fmir::GJumpSubtype::fcmp_une: {
      auto targ = req.operands[0];
      auto a = req.operands[1];
      auto b = req.operands[2];
      bool ordered = true;
      ZydisMnemonic mem = ZYDIS_MNEMONIC_INVALID;

      switch ((fmir::GJumpSubtype)instr.sop) {
        case fmir::GJumpSubtype::fcmp_isNaN:
          ordered = false;
          mem = ZYDIS_MNEMONIC_SETP;
          break;
        case fmir::GJumpSubtype::fcmp_ueq:
          ordered = false;
        case fmir::GJumpSubtype::fcmp_oeq:
          mem = ZYDIS_MNEMONIC_SETZ;
          break;
        case fmir::GJumpSubtype::fcmp_ugt:
          ordered = false;
        case fmir::GJumpSubtype::fcmp_ogt:
          mem = ZYDIS_MNEMONIC_SETNBE;
          break;
        case fmir::GJumpSubtype::fcmp_uge:
          ordered = false;
        case fmir::GJumpSubtype::fcmp_oge:
          mem = ZYDIS_MNEMONIC_SETNB;
          break;
        case fmir::GJumpSubtype::fcmp_ult:
          ordered = false;
        case fmir::GJumpSubtype::fcmp_olt:
          mem = ZYDIS_MNEMONIC_SETB;
          break;
        case fmir::GJumpSubtype::fcmp_ule:
          ordered = false;
        case fmir::GJumpSubtype::fcmp_ole:
          mem = ZYDIS_MNEMONIC_SETBE;
          break;
        case fmir::GJumpSubtype::fcmp_une:
          ordered = false;
        case fmir::GJumpSubtype::fcmp_one:
          TODO("impl");
        case fmir::GJumpSubtype::fcmp_ord:
          TODO("impl");
        case fmir::GJumpSubtype::fcmp_uno:
          TODO("impl");
        default:
          UNREACH();
      }

      bool is_f32 = instr.args[1].ty == fmir::Type::Float32;
      if (ordered) {
        req.mnemonic = is_f32 ? ZYDIS_MNEMONIC_VCOMISS : ZYDIS_MNEMONIC_VCOMISD;
      } else {
        req.mnemonic =
            is_f32 ? ZYDIS_MNEMONIC_VUCOMISS : ZYDIS_MNEMONIC_VUCOMISD;
      }
      req.operands[0] = a;
      req.operands[1] = b;
      req.operand_count = 2;
      u64 off = 0;
      off = emit(out_buff, off, &req);
      req.mnemonic = mem;
      req.operands[0] = targ;
      req.operand_count = 1;
      return emit(out_buff, off, &req);
    }
    case fmir::GJumpSubtype::cjmp_flt_oeq:
    case fmir::GJumpSubtype::cjmp_flt_ogt:
    case fmir::GJumpSubtype::cjmp_flt_oge:
    case fmir::GJumpSubtype::cjmp_flt_olt:
    case fmir::GJumpSubtype::cjmp_flt_ole:
    case fmir::GJumpSubtype::cjmp_flt_one:
    case fmir::GJumpSubtype::cjmp_flt_ord:
    case fmir::GJumpSubtype::cjmp_flt_uno:
    case fmir::GJumpSubtype::cjmp_flt_ueq:
    case fmir::GJumpSubtype::cjmp_flt_ugt:
    case fmir::GJumpSubtype::cjmp_flt_uge:
    case fmir::GJumpSubtype::cjmp_flt_ult:
    case fmir::GJumpSubtype::cjmp_flt_ule:
    case fmir::GJumpSubtype::cjmp_flt_une: {
      bool ordered = true;
      ZydisMnemonic mem = ZYDIS_MNEMONIC_INVALID;

      switch ((fmir::GJumpSubtype)instr.sop) {
        case fmir::GJumpSubtype::cjmp_flt_ueq:
          ordered = false;
        case fmir::GJumpSubtype::cjmp_flt_oeq:
          mem = ZYDIS_MNEMONIC_JZ;
          break;
        case fmir::GJumpSubtype::cjmp_flt_ugt:
          ordered = false;
        case fmir::GJumpSubtype::cjmp_flt_ogt:
          mem = ZYDIS_MNEMONIC_JNBE;
          break;
        case fmir::GJumpSubtype::cjmp_flt_uge:
          ordered = false;
        case fmir::GJumpSubtype::cjmp_flt_oge:
          mem = ZYDIS_MNEMONIC_JNB;
          break;
        case fmir::GJumpSubtype::cjmp_flt_ult:
          ordered = false;
        case fmir::GJumpSubtype::cjmp_flt_olt:
          mem = ZYDIS_MNEMONIC_JB;
          break;
        case fmir::GJumpSubtype::cjmp_flt_ule:
        case fmir::GJumpSubtype::cjmp_flt_ole:
          TODO("impl");
        case fmir::GJumpSubtype::cjmp_flt_une:
          ordered = false;
        case fmir::GJumpSubtype::cjmp_flt_one:
          mem = ZYDIS_MNEMONIC_JNZ;
          break;
        case fmir::GJumpSubtype::cjmp_flt_ord:
          TODO("impl");
        case fmir::GJumpSubtype::cjmp_flt_uno:
          TODO("impl");
        default:
          UNREACH();
      }

      bool is_f32 = instr.args[1].ty == fmir::Type::Float32;
      if (ordered) {
        req.mnemonic = is_f32 ? ZYDIS_MNEMONIC_VCOMISS : ZYDIS_MNEMONIC_VCOMISD;
      } else {
        req.mnemonic =
            is_f32 ? ZYDIS_MNEMONIC_VUCOMISS : ZYDIS_MNEMONIC_VUCOMISD;
      }
      req.operand_count = 2;
      u64 off = 0;
      off = emit(out_buff, off, &req);
      req.mnemonic = mem;
      req.branch_type = ZYDIS_BRANCH_TYPE_NEAR;
      req.operands[0].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
      req.operands[0].imm.s = 0;
      req.operand_count = 1;
      ASSERT(instr.has_bb_ref);
      reloc_map.insert_bb_ref(instr.bb_ref, out_buff + off, 0,
                              RelocSection::Text);
      return emit(out_buff, off, &req);
    }
    case fmir::GJumpSubtype::icmp_mul_overflow: {
      auto targ = req.operands[0];
      auto a = req.operands[1];
      auto b = req.operands[2];
      auto targ_expanded = reg_with_type(instr.args[0].reg, instr.args[1].ty);

      req.mnemonic = ZYDIS_MNEMONIC_IMUL;
      req.operands[0].reg.value = targ_expanded;
      req.operands[1] = a;
      req.operands[2] = b;
      req.operand_count = 3;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      size_t len2 = 32;
      req.mnemonic = ZYDIS_MNEMONIC_SETO;
      req.operands[0] = targ;
      req.operand_count = 1;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff + length, &len2),
                 req);
      return length + len2;
    }
    case fmir::GJumpSubtype::icmp_add_overflow:
    case fmir::GJumpSubtype::INVALID:
      fmt::println("{:cd}", instr);
      TODO("impl");
  }
}

size_t emit_gconv(ZydisEncoderRequest &req, const fmir::MInstr &instr,
                  u8 *const out_buff, u8 curr_bb_id, TLabelUsageMap &reloc_map,
                  ProEpilogueType proepiloguetype) {
  (void)req;
  (void)instr;
  (void)out_buff;
  (void)curr_bb_id;
  (void)reloc_map;
  (void)proepiloguetype;
  size_t length = 9999;
  switch ((fmir::GConvSubtype)instr.sop) {
    case fmir::GConvSubtype::mov_zx:
      if (instr.args[0].ty == instr.args[1].ty) {
        req.mnemonic = ZYDIS_MNEMONIC_MOV;
      } else if (instr.args[1].isReg() &&
                 instr.args[0].ty == fmir::Type::Int64 &&
                 instr.args[1].ty == fmir::Type::Int32) {
        req.mnemonic = ZYDIS_MNEMONIC_MOV;
        req.operands[1].reg.value =
            reg_with_type(instr.args[1].reg, fmir::Type::Int64);
        // auto increased_reg = instr.args[1].reg;
        // increased_reg.size() = 8;
        // req.operands[1].reg.value = convert_reg(increased_reg);
      } else {
        req.mnemonic = ZYDIS_MNEMONIC_MOVZX;
      }
      return emit(out_buff, 0, &req);
    case fmir::GConvSubtype::itrunc: {
      req.mnemonic = ZYDIS_MNEMONIC_MOV;
      if (instr.args[1].isReg() && instr.args[0].ty == fmir::Type::Int64) {
        req.operands[1].reg.value =
            reg_with_type(instr.args[1].reg, fmir::Type::Int64);
        return emit(out_buff, 0, &req);
      }
      if (instr.args[1].isReg() && instr.args[0].ty == fmir::Type::Int32) {
        req.operands[1].reg.value =
            reg_with_type(instr.args[1].reg, fmir::Type::Int32);
        return emit(out_buff, 0, &req);
      }
      if (instr.args[1].isReg() && instr.args[0].ty == fmir::Type::Int16) {
        req.operands[1].reg.value =
            reg_with_type(instr.args[1].reg, fmir::Type::Int16);
        size_t off = 0;
        off = emit(out_buff, off, &req);
        // mask out the higher bits since it doesnt do this for 16 and 8 bit
        req.mnemonic = ZYDIS_MNEMONIC_AND;
        req.operands[0].reg.value =
            reg_with_type(instr.args[0].reg, fmir::Type::Int32);
        req.operands[1].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
        req.operands[1].imm.u = 0xFFFF;
        return emit(out_buff, off, &req);
      }
      if (instr.args[1].isReg() && instr.args[0].ty == fmir::Type::Int8) {
        // TODO: could move this into the matcher that would allow for
        // optimizations if known bits are there
        req.operands[1].reg.value =
            reg_with_type(instr.args[1].reg, fmir::Type::Int8);
        size_t off = 0;
        off = emit(out_buff, off, &req);
        // mask out the higher bits since it doesnt do this for 16 and 8 bit
        //  NOTE: In 64-bit mode, r/m8 can not be encoded to access the
        // following
        //  byte registers if a REX prefix is used: AH, BH, CH, DH.
        //  https://www.felixcloutier.com/x86/and
        req.mnemonic = ZYDIS_MNEMONIC_AND;
        req.operands[0].reg.value =
            reg_with_type(instr.args[0].reg, fmir::Type::Int16);
        req.operands[1].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
        req.operands[1].imm.u = 0xFF;
        return emit(out_buff, off, &req);
      }
      fmt::println("implit {}", instr);
      TODO("UNREACH");
    }
    case fmir::GConvSubtype::mov_sx:
      if (instr.args[0].ty == instr.args[1].ty) {
        req.mnemonic = ZYDIS_MNEMONIC_MOV;
      } else if (4 == get_size(instr.args[1].ty)) {
        req.mnemonic = ZYDIS_MNEMONIC_MOVSXD;
      } else {
        req.mnemonic = ZYDIS_MNEMONIC_MOVSX;
      }
      return emit(out_buff, 0, &req);
    case fmir::GConvSubtype::SI2FL:
      req.mnemonic = instr.args[0].ty == fmir::Type::Float32
                         ? ZYDIS_MNEMONIC_CVTSI2SS
                         : ZYDIS_MNEMONIC_CVTSI2SD;
      return emit(out_buff, 0, &req);
    case fmir::GConvSubtype::F64_ext:
      ASSERT(instr.args[1].ty == fmir::Type::Float32);
      req.mnemonic = ZYDIS_MNEMONIC_CVTSS2SD;
      return emit(out_buff, 0, &req);
    case fmir::GConvSubtype::F32_trunc:
      ASSERT(instr.args[1].ty == fmir::Type::Float64);
      req.mnemonic = ZYDIS_MNEMONIC_CVTSD2SS;
      return emit(out_buff, 0, &req);
    case fmir::GConvSubtype::FL2UI:
    case fmir::GConvSubtype::FL2SI: {
      bool is_f32 = instr.args[1].ty == fmir::Type::Float32;
      bool out_64 = instr.args[0].ty == fmir::Type::Int64;
      bool out_32 = instr.args[0].ty == fmir::Type::Int32;
      bool is_unsigned = instr.is(fmir::GConvSubtype::FL2UI);
      ASSERT(req.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER);
      ASSERT(req.operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER);

      req.mnemonic =
          is_f32 ? ZYDIS_MNEMONIC_VCVTTSS2SI : ZYDIS_MNEMONIC_VCVTTSD2SI;
      if (out_32 && is_unsigned) {
        req.operands[0].reg.value =
            reg_with_type(instr.args[0].reg, fmir::Type::Int64);
      } else if (!out_64) {
        req.operands[0].reg.value =
            reg_with_type(instr.args[0].reg, fmir::Type::Int32);
      }
      return emit(out_buff, 0, &req);
    }
    case fmir::GConvSubtype::UI2FL: {
      bool is_f32 = instr.args[1].ty == fmir::Type::Float32;
      req.mnemonic =
          is_f32 ? ZYDIS_MNEMONIC_VCVTUSI2SS : ZYDIS_MNEMONIC_VCVTUSI2SD;
      ASSERT(req.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER);
      ASSERT(req.operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER);
      ASSERT(get_size(instr.args[1].ty) == 4 ||
             get_size(instr.args[1].ty) == 8);
      // Weird instruction just gonna do this
      req.operand_count = 3;
      req.operands[2] = req.operands[1];
      req.operands[1] = req.operands[0];
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    }
    case fmir::GConvSubtype::INVALID:
      fmt::println("{:cd}", instr);
      TODO("impl");
  }
}

size_t emit_garith(ZydisEncoderRequest &req, const fmir::MInstr &instr,
                   u8 *const out_buff, u8 curr_bb_id, TLabelUsageMap &reloc_map,
                   ProEpilogueType proepiloguetype) {
  size_t length = 9999;
  (void)req;
  (void)instr;
  (void)out_buff;
  (void)curr_bb_id;
  (void)reloc_map;
  (void)proepiloguetype;
  switch ((fmir::GArithSubtype)instr.sop) {
    case fmir::GArithSubtype::sub2:
      req.mnemonic = ZYDIS_MNEMONIC_SUB;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    case fmir::GArithSubtype::udiv:
    case fmir::GArithSubtype::idiv:
      return emit_div(instr, req, out_buff);
    case fmir::GArithSubtype::add2:
      req.mnemonic = ZYDIS_MNEMONIC_ADD;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    case fmir::GArithSubtype::land2:
      req.mnemonic = ZYDIS_MNEMONIC_AND;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    case fmir::GArithSubtype::lor2:
      req.mnemonic = ZYDIS_MNEMONIC_OR;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    case fmir::GArithSubtype::lxor2:
      req.mnemonic = ZYDIS_MNEMONIC_XOR;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    case fmir::GArithSubtype::not1:
      req.mnemonic = ZYDIS_MNEMONIC_NOT;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    case fmir::GArithSubtype::neg1:
      req.mnemonic = ZYDIS_MNEMONIC_NEG;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    case fmir::GArithSubtype::mul2: {
      req.mnemonic = ZYDIS_MNEMONIC_IMUL;
      // TODO: need to check if its fits correclty
      i32 val_casted = (i32)(i64)instr.args[1].imm;
      if (req.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE &&
          val_casted >= std::numeric_limits<i32>::min() &&
          val_casted <= std::numeric_limits<i32>::max()) {
        req.operands[2] = req.operands[1];
        req.operands[1] = req.operands[0];
        req.operand_count = 3;
      } else if (req.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
        TODO("cant do this big of a constant");
      }
      return emit(out_buff, 0, &req);
    }
    case fmir::GArithSubtype::smul3: {
      req.mnemonic = ZYDIS_MNEMONIC_IMUL;
      // TODO: need to check if its fits correclty
      i64 val_casted = (i64)instr.args[1].imm;
      if (req.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE &&
          (val_casted <= std::numeric_limits<i32>::min() ||
           val_casted >= std::numeric_limits<i32>::max())) {
        TODO("cant do this big of a constant");
      }
      return emit(out_buff, 0, &req);
    }
    case fmir::GArithSubtype::sar2:
      req.mnemonic = ZYDIS_MNEMONIC_SAR;
      return emit(out_buff, 0, &req);
    case fmir::GArithSubtype::shl2:
      req.mnemonic = ZYDIS_MNEMONIC_SHL;
      return emit(out_buff, 0, &req);
    case fmir::GArithSubtype::shr2:
      req.mnemonic = ZYDIS_MNEMONIC_SHR;
      return emit(out_buff, 0, &req);
    case fmir::GArithSubtype::abs: {
      u64 off = 0;
      // auto target = req.operands[0];
      // auto arg = req.operands[1];
      //  mov     eax, edi
      //  neg     eax
      //  cmovs   eax, edi
      //  ret
      req.mnemonic = ZYDIS_MNEMONIC_MOV;
      off = emit(out_buff, off, &req);
      req.mnemonic = ZYDIS_MNEMONIC_NEG;
      req.operand_count = 1;
      off = emit(out_buff, off, &req);
      req.mnemonic = ZYDIS_MNEMONIC_CMOVS;
      req.operand_count = 2;
      off = emit(out_buff, off, &req);
      return off;
    }
    case fmir::GArithSubtype::INVALID:
      fmt::println("{:cd}", instr);
      TODO("impl");
  }
}
size_t emit_gcmov(ZydisEncoderRequest &req, const fmir::MInstr &instr,
                  u8 *const out_buff, u8 curr_bb_id, TLabelUsageMap &reloc_map,
                  ProEpilogueType proepiloguetype) {
  (void)req;
  (void)instr;
  (void)out_buff;
  (void)curr_bb_id;
  (void)reloc_map;
  (void)proepiloguetype;
  switch ((fmir::GCMovSubtype)instr.sop) {
    case fmir::GCMovSubtype::cmov_sgt:
    case fmir::GCMovSubtype::cmov_slt:
    case fmir::GCMovSubtype::cmov_ult:
    case fmir::GCMovSubtype::cmov_sge:
    case fmir::GCMovSubtype::cmov_sle:
    case fmir::GCMovSubtype::cmov_ne:
    case fmir::GCMovSubtype::cmov_eq:
    case fmir::GCMovSubtype::cmov_ugt:
    case fmir::GCMovSubtype::cmov_uge:
    case fmir::GCMovSubtype::cmov_ule:
    case fmir::GCMovSubtype::cmov_ns: {
      ASSERT(!instr.args[0].is_vec_reg());
      ASSERT(!instr.args[1].is_vec_reg());
      ASSERT(!instr.args[2].is_vec_reg());
      ASSERT(!instr.args[3].is_vec_reg());

      auto targ = req.operands[0];
      auto val = req.operands[1];
      auto c1 = req.operands[2];
      auto c2 = req.operands[3];
      u64 off = 0;

      {  // test
        req.operand_count = 2;
        req.operands[0] = c1;
        req.operands[1] = c2;
        switch ((fmir::GCMovSubtype)instr.sop) {
          case fmir::GCMovSubtype::cmov_ns:
            req.mnemonic = ZYDIS_MNEMONIC_TEST;
            break;
          case fmir::GCMovSubtype::cmov_slt:
          case fmir::GCMovSubtype::cmov_sgt:
          case fmir::GCMovSubtype::cmov_ult:
          case fmir::GCMovSubtype::cmov_sge:
          case fmir::GCMovSubtype::cmov_sle:
          case fmir::GCMovSubtype::cmov_ugt:
          case fmir::GCMovSubtype::cmov_uge:
          case fmir::GCMovSubtype::cmov_ne:
          case fmir::GCMovSubtype::cmov_eq:
          case fmir::GCMovSubtype::cmov_ule:
            req.mnemonic = ZYDIS_MNEMONIC_CMP;
            break;
          default:
            fmt::println("{}", instr);
            TODO("impl");
        }
        off = emit(out_buff, off, &req);
      }

      {  // cmov
        req.operand_count = 2;
        req.operands[0] = targ;
        req.operands[1] = val;
        switch ((fmir::GCMovSubtype)instr.sop) {
          case fmir::GCMovSubtype::cmov_ns:
            req.mnemonic = ZYDIS_MNEMONIC_CMOVNS;
            break;
          case fmir::GCMovSubtype::cmov_sgt:
            req.mnemonic = ZYDIS_MNEMONIC_CMOVNLE;
            break;
          case fmir::GCMovSubtype::cmov_uge:
            req.mnemonic = ZYDIS_MNEMONIC_CMOVNB;
            break;
          case fmir::GCMovSubtype::cmov_ne:
            req.mnemonic = ZYDIS_MNEMONIC_CMOVNZ;
            break;
          case fmir::GCMovSubtype::cmov_eq:
            req.mnemonic = ZYDIS_MNEMONIC_CMOVZ;
            break;
          case fmir::GCMovSubtype::cmov_ult:
            req.mnemonic = ZYDIS_MNEMONIC_CMOVB;
            break;
          case fmir::GCMovSubtype::cmov_slt:
            req.mnemonic = ZYDIS_MNEMONIC_CMOVL;
            break;
          case fmir::GCMovSubtype::cmov_sge:
            req.mnemonic = ZYDIS_MNEMONIC_CMOVNL;
            break;
          case fmir::GCMovSubtype::cmov_sle:
            req.mnemonic = ZYDIS_MNEMONIC_CMOVLE;
            break;
          case fmir::GCMovSubtype::cmov_ugt:
            req.mnemonic = ZYDIS_MNEMONIC_CMOVNBE;
            break;
          case fmir::GCMovSubtype::cmov_ule:
            req.mnemonic = ZYDIS_MNEMONIC_CMOVBE;
            break;
          default:
            UNREACH();
        }
      }
      return emit(out_buff, off, &req);
    }
    case fmir::GCMovSubtype::cmov: {
      auto targ = req.operands[0];
      auto cond = req.operands[1];
      auto value = req.operands[2];
      if (instr.args[0].ty == fmir::Type::Float32 ||
          instr.args[0].ty == fmir::Type::Float64) {
        bool isf32 = instr.args[0].ty == fmir::Type::Float32;
        req.mnemonic = ZYDIS_MNEMONIC_TEST;
        req.operand_count = 2;
        req.operands[0] = cond;
        req.operands[1] = cond;
        u64 off = 0;
        off = emit(out_buff, off, &req);
        auto off_start_jmp = off;
        req.mnemonic = ZYDIS_MNEMONIC_JZ;
        req.operand_count = 1;
        req.operands[0].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
        req.operands[0].imm.s = 16;
        off = emit(out_buff, off, &req);
        auto old_jmp_end = off;
        req.mnemonic = isf32 ? ZYDIS_MNEMONIC_MOVSS : ZYDIS_MNEMONIC_MOVSD;
        req.operand_count = 2;
        req.operands[0] = targ;
        req.operands[1] = value;
        auto last_instr_off = emit(out_buff, off, &req);
        auto last_instr_size = last_instr_off - off;
        ASSERT(last_instr_size <= 16);

        req.mnemonic = ZYDIS_MNEMONIC_JZ;
        req.operand_count = 1;
        req.operands[0].type = ZYDIS_OPERAND_TYPE_IMMEDIATE;
        req.operands[0].imm.s = (i64)last_instr_size;
        u64 new_jmp_end = emit(out_buff, off_start_jmp, &req);
        ASSERT(new_jmp_end == old_jmp_end);

        return last_instr_off;
      }
      req.mnemonic = ZYDIS_MNEMONIC_TEST;
      req.operand_count = 2;
      req.operands[0] = cond;
      req.operands[1] = cond;
      u64 off = 0;
      off = emit(out_buff, off, &req);
      req.mnemonic = ZYDIS_MNEMONIC_CMOVNZ;
      req.operand_count = 2;
      req.operands[0] = targ;
      req.operands[1] = value;
      if (get_size(instr.args[0].ty) == 1 &&
          value.type == ZYDIS_OPERAND_TYPE_REGISTER) {
        req.operands[0].reg.value =
            reg_with_type(instr.args[0].reg, fmir::Type::Int64);
        req.operands[1].reg.value =
            reg_with_type(instr.args[2].reg, fmir::Type::Int64);
      }
      return emit(out_buff, off, &req);
    }
    case fmir::GCMovSubtype::INVALID:
      fmt::println("{:cd}", instr);
      TODO("impl");
  }
}
size_t emit_gvec(ZydisEncoderRequest &req, const fmir::MInstr &instr,
                 u8 *const out_buff, u8 curr_bb_id, TLabelUsageMap &reloc_map,
                 ProEpilogueType proepiloguetype) {
  (void)req;
  (void)instr;
  (void)out_buff;
  (void)curr_bb_id;
  (void)reloc_map;
  (void)proepiloguetype;
  size_t length = 9999;
  switch ((fmir::GVecSubtype)instr.sop) {
    case fmir::GVecSubtype::fmul:
      switch (instr.args[0].ty) {
        default:
          TODO("UNREACH");
        case fmir::Type::Float32:
          req.mnemonic = ZYDIS_MNEMONIC_VMULSS;
          break;
        case fmir::Type::Float64:
          req.mnemonic = ZYDIS_MNEMONIC_VMULSD;
          break;
        case fmir::Type::Float32x4:
        case fmir::Type::Float32x8:
          req.mnemonic = ZYDIS_MNEMONIC_VMULPS;
          break;
        case fmir::Type::Float64x4:
        case fmir::Type::Float64x2:
          req.mnemonic = ZYDIS_MNEMONIC_VMULPD;
          break;
      }
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    case fmir::GVecSubtype::fdiv:
      req.mnemonic = instr.args[0].ty == fmir::Type::Float32
                         ? ZYDIS_MNEMONIC_VDIVSS
                         : ZYDIS_MNEMONIC_VDIVSD;
      ZY_ASS(ZydisEncoderEncodeInstruction(&req, out_buff, &length));
      return length;
    case fmir::GVecSubtype::vadd:
      switch (instr.args[0].ty) {
        default:
          TODO("UNREACH");
        case fmir::Type::Float32:
          req.mnemonic = ZYDIS_MNEMONIC_VADDSS;
          break;
        case fmir::Type::Float64:
          req.mnemonic = ZYDIS_MNEMONIC_VADDSD;
          break;
        case fmir::Type::Float32x4:
        case fmir::Type::Float32x8:
          req.mnemonic = ZYDIS_MNEMONIC_VADDPS;
          break;
        case fmir::Type::Float64x4:
        case fmir::Type::Float64x2:
          req.mnemonic = ZYDIS_MNEMONIC_VADDPD;
          break;
        case fmir::Type::Int32x4:
        case fmir::Type::Int32x8:
          req.mnemonic = ZYDIS_MNEMONIC_VPADDD;
          break;
        case fmir::Type::Int64x2:
        case fmir::Type::Int64x4:
          req.mnemonic = ZYDIS_MNEMONIC_VPADDQ;
          break;
      }
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    case fmir::GVecSubtype::vsub:
      switch (instr.args[0].ty) {
        default:
          TODO("UNREACH");
        case fmir::Type::Float32:
          req.mnemonic = ZYDIS_MNEMONIC_VSUBSS;
          break;
        case fmir::Type::Float64:
          req.mnemonic = ZYDIS_MNEMONIC_VSUBSD;
          break;
        case fmir::Type::Float32x4:
        case fmir::Type::Float32x8:
          req.mnemonic = ZYDIS_MNEMONIC_VSUBPS;
          break;
        case fmir::Type::Float64x4:
        case fmir::Type::Float64x2:
          req.mnemonic = ZYDIS_MNEMONIC_VSUBPD;
          break;
        case fmir::Type::Int32x4:
          req.mnemonic = ZYDIS_MNEMONIC_PSUBD;
          break;
        case fmir::Type::Int32x8:
          req.mnemonic = ZYDIS_MNEMONIC_VPSUBD;
          break;
        case fmir::Type::Int64x2:
          req.mnemonic = ZYDIS_MNEMONIC_PSUBQ;
          break;
        case fmir::Type::Int64x4:
          req.mnemonic = ZYDIS_MNEMONIC_VPSUBQ;
          break;
      }
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    case fmir::GVecSubtype::fOr:
      req.mnemonic = instr.args[0].ty == fmir::Type::Float32
                         ? ZYDIS_MNEMONIC_VORPS
                         : ZYDIS_MNEMONIC_VORPD;
      return emit(out_buff, 0, &req);
    case fmir::GVecSubtype::fAnd:
      req.mnemonic = instr.args[0].ty == fmir::Type::Float32
                         ? ZYDIS_MNEMONIC_VANDPS
                         : ZYDIS_MNEMONIC_VANDPD;
      return emit(out_buff, 0, &req);
    case fmir::GVecSubtype::fxor:
      req.mnemonic = instr.args[0].ty == fmir::Type::Float32
                         ? ZYDIS_MNEMONIC_VXORPS
                         : ZYDIS_MNEMONIC_VXORPD;
      return emit(out_buff, 0, &req);
    case fmir::GVecSubtype::fShl:
      switch (instr.args[0].ty) {
        case fmir::Type::Int32x4:
        case fmir::Type::Float32x2:
        case fmir::Type::Int32x8:
        case fmir::Type::Float32x4:
        case fmir::Type::Float32x8:
          req.mnemonic = ZYDIS_MNEMONIC_VPSLLVD;
          break;
        case fmir::Type::Int64x2:
        case fmir::Type::Float64x2:
        case fmir::Type::Int64x4:
        case fmir::Type::Float64x4:
          // TOdO: idk if this is avx512 some sources list it there but most
          // under avx2
          req.mnemonic = ZYDIS_MNEMONIC_VPSLLVQ;
          break;
        case fmir::Type::INVALID:
        case fmir::Type::Int8:
        case fmir::Type::Int16:
        case fmir::Type::Int32:
        case fmir::Type::Int64:
        case fmir::Type::Float32:
        case fmir::Type::Float64:
          fmt::println("{}", instr);
          TODO("Impl");
      }
      return emit(out_buff, 0, &req);
    case fmir::GVecSubtype::ffmadd:
    case fmir::GVecSubtype::INVALID:
      fmt::println("{:cd}", instr);
      TODO("impl");
    case fmir::GVecSubtype::fMax:
      switch (instr.args[0].ty) {
        default:
          fmt::println("{}", instr);
          TODO("Impl");
        case fmir::Type::Float32:
          req.mnemonic = ZYDIS_MNEMONIC_VMAXSS;
          break;
        case fmir::Type::Float64:
          req.mnemonic = ZYDIS_MNEMONIC_VMAXSD;
          break;
        case fmir::Type::Float32x2:
        case fmir::Type::Float32x4:
        case fmir::Type::Float32x8:
          req.mnemonic = ZYDIS_MNEMONIC_VMAXPS;
          break;
        case fmir::Type::Float64x2:
        case fmir::Type::Float64x4:
          req.mnemonic = ZYDIS_MNEMONIC_VMAXPD;
          break;
      }
      return emit(out_buff, 0, &req);
    case fmir::GVecSubtype::fMin:
      switch (instr.args[0].ty) {
        default:
          fmt::println("{}", instr);
          TODO("Impl");
        case fmir::Type::Float32:
          req.mnemonic = ZYDIS_MNEMONIC_VMINSS;
          break;
        case fmir::Type::Float64:
          req.mnemonic = ZYDIS_MNEMONIC_VMINSD;
          break;
        case fmir::Type::Float32x2:
        case fmir::Type::Float32x4:
        case fmir::Type::Float32x8:
          req.mnemonic = ZYDIS_MNEMONIC_VMINPS;
          break;
        case fmir::Type::Float64x2:
        case fmir::Type::Float64x4:
          req.mnemonic = ZYDIS_MNEMONIC_VMINPD;
          break;
      }
      return emit(out_buff, 0, &req);
  }
}
size_t emit_x86(ZydisEncoderRequest &req, const fmir::MInstr &instr,
                u8 *const out_buff, u8 curr_bb_id, TLabelUsageMap &reloc_map,
                ProEpilogueType proepiloguetype) {
  (void)req;
  (void)instr;
  (void)out_buff;
  (void)curr_bb_id;
  (void)reloc_map;
  (void)proepiloguetype;
  size_t length = 9999;
  switch ((fmir::X86Subtype)instr.sop) {
    case fmir::X86Subtype::lea:
      req.mnemonic = ZYDIS_MNEMONIC_LEA;
      return emit(out_buff, 0, &req);
    case fmir::X86Subtype::lzcnt:
      req.mnemonic = ZYDIS_MNEMONIC_LZCNT;
      return emit(out_buff, 0, &req);
    case fmir::X86Subtype::ffmadd231: {
      bool is_f32 = instr.args[1].ty == fmir::Type::Float32;
      req.mnemonic =
          is_f32 ? ZYDIS_MNEMONIC_VFMADD231SS : ZYDIS_MNEMONIC_VFMADD231SD;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    }
    case fmir::X86Subtype::ffmadd132: {
      bool is_f32 = instr.args[1].ty == fmir::Type::Float32;
      req.mnemonic =
          is_f32 ? ZYDIS_MNEMONIC_VFMADD132SS : ZYDIS_MNEMONIC_VFMADD132SD;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    }
    case fmir::X86Subtype::ffmadd213: {
      bool is_f32 = instr.args[1].ty == fmir::Type::Float32;
      req.mnemonic =
          is_f32 ? ZYDIS_MNEMONIC_VFMADD213SS : ZYDIS_MNEMONIC_VFMADD213SD;
      ZY_ASS_REQ(ZydisEncoderEncodeInstruction(&req, out_buff, &length), req);
      return length;
    }
    case fmir::X86Subtype::punpckl: {
      assert(req.operand_count == 3);
      ASSERT(instr.args[0].isReg());
      switch (instr.args[0].ty) {
        case fmir::Type::INVALID:
        case fmir::Type::Int8:
        case fmir::Type::Int16:
        case fmir::Type::Int32:
        case fmir::Type::Int64:
        case fmir::Type::Float32:
        case fmir::Type::Float64:
          TODO("UNREACH");
          // req.mnemonic = ZYDIS_MNEMONIC_VPUNPCKLBW;
          // req.mnemonic = ZYDIS_MNEMONIC_VPUNPCKLWD;
        case fmir::Type::Int32x4:
        case fmir::Type::Float32x2:
        case fmir::Type::Float32x4:
        case fmir::Type::Int32x8:
        case fmir::Type::Float32x8:
          req.mnemonic = ZYDIS_MNEMONIC_VPUNPCKLDQ;
          break;
        case fmir::Type::Int64x2:
        case fmir::Type::Float64x2:
        case fmir::Type::Int64x4:
        case fmir::Type::Float64x4:
          req.mnemonic = ZYDIS_MNEMONIC_VPUNPCKLQDQ;
          break;
      }
      return emit(out_buff, 0, &req);
    }
    case fmir::X86Subtype::vpshuf: {
      assert(req.operand_count == 3);
      ASSERT(instr.args[0].isReg());
      switch (instr.args[0].ty) {
        case fmir::Type::Int32x4:
        case fmir::Type::Float32x2:
        case fmir::Type::Float32x4:
        case fmir::Type::Int32x8:
        case fmir::Type::Float32x8:
          req.mnemonic = ZYDIS_MNEMONIC_VPSHUFD;
          break;
        case fmir::Type::INVALID:
        case fmir::Type::Int8:
        case fmir::Type::Int16:
        case fmir::Type::Int32:
        case fmir::Type::Int64:
        case fmir::Type::Float32:
        case fmir::Type::Float64:
        case fmir::Type::Int64x2:
        case fmir::Type::Float64x2:
        case fmir::Type::Int64x4:
        case fmir::Type::Float64x4:
          TODO("UNREACH");
          break;
      }
      return emit(out_buff, 0, &req);
    }
    case fmir::X86Subtype::vbroadcast: {
      assert(req.operand_count == 2);
      ASSERT(instr.args[0].isReg());
      switch (instr.args[0].ty) {
        case fmir::Type::INVALID:
        case fmir::Type::Int8:
        case fmir::Type::Int16:
        case fmir::Type::Int32:
        case fmir::Type::Int64:
        case fmir::Type::Float32:
        case fmir::Type::Float64:
          TODO("UNREACH");
        case fmir::Type::Int32x4:
        case fmir::Type::Float32x2:
        case fmir::Type::Float32x4:
        case fmir::Type::Int32x8:
        case fmir::Type::Float32x8:
          req.mnemonic = ZYDIS_MNEMONIC_VBROADCASTSS;
          break;
        case fmir::Type::Int64x2:
        case fmir::Type::Float64x2:
        case fmir::Type::Int64x4:
        case fmir::Type::Float64x4:
          req.mnemonic = ZYDIS_MNEMONIC_VBROADCASTSD;
          break;
      }
      return emit(out_buff, 0, &req);
    }
    case fmir::X86Subtype::INVALID:
      fmt::println("{:cd}", instr);
      TODO("impl");
  }
}

}  // namespace
}  // namespace foptim::codegen
