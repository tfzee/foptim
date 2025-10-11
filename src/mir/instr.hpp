#pragma once
#include "ir/instruction_data.hpp"
#include "utils/todo.hpp"
#include "utils/types.hpp"

namespace foptim::fmir {

enum class GOpcode : u8 {
  GBase,
  GJmp,
  GConv,
  GArith,
  GCMov,
  GVec,
  X86,
};

enum class GVecSubtype : u32 {
  INVALID = 0,
  vadd,
  vsub,
  fmul,
  fdiv,
  ffmadd,
  fxor,
  fAnd,
  fOr,
  fShl,
  fMax,
  fMin,
};

enum class GJumpSubtype : u32 {
  INVALID = 0,
  icmp_slt,
  icmp_eq,
  icmp_ult,
  icmp_ne,
  icmp_sgt,
  icmp_ugt,
  icmp_uge,
  icmp_ule,
  icmp_sge,
  icmp_sle,
  icmp_mul_overflow,
  icmp_add_overflow,

  fcmp_isNaN,
  fcmp_oeq,
  fcmp_ogt,
  fcmp_oge,
  fcmp_olt,
  fcmp_ole,
  fcmp_one,
  fcmp_ord,
  fcmp_uno,
  fcmp_ueq,
  fcmp_ugt,
  fcmp_uge,
  fcmp_ult,
  fcmp_ule,
  fcmp_une,

  cjmp_and,
  cjmp_or,
  cjmp_int_slt,
  cjmp_int_sge,
  cjmp_int_sle,
  cjmp_int_sgt,
  cjmp_int_ult,
  cjmp_int_ule,
  cjmp_int_ugt,
  cjmp_int_uge,
  cjmp_int_ne,
  cjmp_int_eq,
  cjmp_flt_oeq,
  cjmp_flt_ogt,
  cjmp_flt_oge,
  cjmp_flt_olt,
  cjmp_flt_ole,
  cjmp_flt_one,
  cjmp_flt_ord,
  cjmp_flt_uno,
  cjmp_flt_ueq,
  cjmp_flt_ugt,
  cjmp_flt_uge,
  cjmp_flt_ult,
  cjmp_flt_ule,
  cjmp_flt_une,

  cjmp,
  jmp,
};

enum class GArithSubtype : u32 {
  INVALID = 0,
  abs,
  shl2,
  shr2,
  sar2,
  land2,
  lor2,
  lxor2,
  add2,
  sub2,
  mul2,
  not1,
  neg1,
  idiv,
  udiv,
  smul3,
};

enum class GConvSubtype : u32 {
  INVALID = 0,
  SI2FL,
  UI2FL,
  FL2SI,
  FL2UI,
  F64_ext,
  F32_trunc,
  itrunc,
  // should prob be named just zx and sx not move
  mov_zx,
  mov_sx,
};

enum class GCMovSubtype : u32 {
  INVALID = 0,
  cmov,
  cmov_ns,
  cmov_sgt,
  cmov_slt,
  cmov_ult,
  cmov_sge,
  cmov_sle,
  cmov_ne,
  cmov_eq,
  cmov_ugt,
  cmov_uge,
  cmov_ule,
};

enum class GBaseSubtype : u32 {
  INVALID = 0,
  // could be named copy or so to differentiate it idk
  mov,
  push,
  pop,
  call,
  ret,

  // special purpose helper instructions
  //  that *cant* be generated and *need* to be lowered
  // used for each arg for a function call
  arg_setup,
  // used for each function call all arg_setups immediatly prior are the args
  // while this isntruction only takes the function label and the return
  // register as args
  invoke,
};

enum class X86Subtype : u32 {
  INVALID = 0,
  lea,
  vmovshdup,
  vpermil,
  vpshuf,
  punpckl,
  vbroadcast,
  lzcnt,
  ffmadd132,
  ffmadd213,
  ffmadd231,
  HAdd,
  sqrt,
};

const char *getNameFromOpcode(GOpcode code, u32 sop);

enum class Type : u16 {
  INVALID = 0,
  // NOTE: ORDER matters from smallest to biggest!
  Int8 = 1,
  Int16 = 2,
  Int32 = 3,
  Int64 = 4,

  // NOTE: ORDER matters then floating point smallest to biggest
  Float32 = 5,
  Float64 = 6,

  // NOTE: ORDER matters then vec smallest to biggest
  // xmm
  Int32x4 = 7,
  Int64x2 = 8,
  Float32x2 = 9,
  Float32x4 = 10,
  Float64x2 = 11,
  // ymm
  Int32x8 = 12,
  Int64x4 = 13,
  Float32x8 = 14,
  Float64x4 = 15,
};

/*Returns the size in bytes of the given type*/
static constexpr u32 get_size(fmir::Type type) {
  switch (type) {
    case Type::Float32:
      return 4;
    case Type::Float64:
      return 8;
    case Type::Int8:
      return 1;
    case Type::Int16:
      return 2;
    case Type::Int32:
      return 4;
    case Type::Int64:
      return 8;
    case Type::Float32x2:
      return 4 * 2;
    case Type::Int32x4:
      return 4 * 4;
    case Type::Int64x2:
      return 8 * 2;
    case Type::Float32x4:
      return 4 * 4;
    case Type::Float64x2:
      return 8 * 2;
    case Type::Int32x8:
      return 2 * 8;
    case Type::Int64x4:
      return 8 * 4;
    case Type::Float32x8:
      return 4 * 8;
    case Type::Float64x4:
      return 8 * 4;
    case fmir::Type::INVALID:
      TODO("INVALID TYPE");
  }
}

enum class CReg : u8 {
  Virtual = 0,
  A = 1,
  B = 2,
  C = 3,
  D = 4,
  SI = 5,
  DI = 6,
  SP = 7,
  BP = 8,
  R8 = 9,
  R9 = 10,
  R10 = 11,
  R11 = 12,
  R12 = 13,
  R13 = 14,
  R14 = 15,
  R15 = 16,
  mm0 = 17,
  mm1 = 18,
  mm2 = 19,
  mm3 = 20,
  mm4 = 21,
  mm5 = 22,
  mm6 = 23,
  mm7 = 24,
  mm8 = 25,
  mm9 = 26,
  mm10 = 27,
  mm11 = 28,
  mm12 = 29,
  mm13 = 30,
  mm14 = 31,
  mm15 = 32,
  // TODO: support the other registers that come with avx so 16 -> 31
  N_REGS = 33,
};

class VReg {
 public:
  enum class RegType : u8 {
    Virtual,
    Concrete,
  };

  union {
    struct {
      RegType rty;
      Type ty;
    };
    struct {
      RegType rty;
      Type ty;
      u64 id;
    } virt;
    struct {
      RegType rty;
      Type ty;
      CReg creg;
    } conc;
  };

  consteval VReg() : virt(RegType::Virtual, Type::INVALID, 0) {}
  constexpr VReg(u64 id) : virt(RegType::Virtual, Type::INVALID, id) {}
  constexpr VReg(u64 id, Type ty) : virt(RegType::Virtual, ty, id) {}
  constexpr VReg(CReg reg_ty, Type ty = Type::Int64)
      : conc(RegType::Concrete, ty, reg_ty) {}

  [[nodiscard]] constexpr bool is_concrete() const {
    return rty == RegType::Concrete;
  }

  [[nodiscard]] constexpr u64 virt_id() const {
    ASSERT(!is_concrete());
    return virt.id;
  }

  [[nodiscard]] constexpr CReg c_reg() const {
    ASSERT(is_concrete());
    return conc.creg;
  }

  [[nodiscard]] constexpr u64 size() const { return get_size(ty); }

  [[nodiscard]] constexpr bool is_vec_reg() const {
    switch (rty) {
      case RegType::Virtual:
        return ty >= Type::Float32;
      case RegType::Concrete:
        return conc.creg >= CReg::mm0;
    }
  }
  [[nodiscard]] static consteval VReg RDI() { return {CReg::DI, Type::Int64}; }
  [[nodiscard]] static consteval VReg RSI() { return {CReg::SI, Type::Int64}; }
  [[nodiscard]] static consteval VReg RDX() { return {CReg::D, Type::Int64}; }
  [[nodiscard]] static consteval VReg R8() { return {CReg::R8, Type::Int64}; }
  [[nodiscard]] static consteval VReg R9() { return {CReg::R9, Type::Int64}; }
  [[nodiscard]] static consteval VReg RAX() { return {CReg::A, Type::Int64}; }
  [[nodiscard]] static consteval VReg EAX() { return {CReg::A, Type::Int32}; }
  [[nodiscard]] static consteval VReg RCX() { return {CReg::C, Type::Int64}; }
  [[nodiscard]] static consteval VReg ECX() { return {CReg::C, Type::Int32}; }
  [[nodiscard]] static consteval VReg EDX() { return {CReg::D, Type::Int32}; }

  [[nodiscard]] static consteval VReg MM0SS() {
    return {CReg::mm0, Type::Float32};
  }
  [[nodiscard]] static consteval VReg MM1SS() {
    return {CReg::mm1, Type::Float32};
  }

  [[nodiscard]] static consteval VReg RSP() { return {CReg::SP, Type::Int64}; }
  [[nodiscard]] static consteval VReg RBP() { return {CReg::BP, Type::Int64}; }
  [[nodiscard]] static consteval VReg CL() { return {CReg::C, Type::Int8}; }

  [[nodiscard]] constexpr bool operator==(const VReg &other) const {
    if (rty != other.rty) {
      return false;
    }
    switch (rty) {
      case RegType::Virtual:
        return virt.id == other.virt.id;
      case RegType::Concrete:
        return conc.creg == other.conc.creg;
        break;
    }
  }
};

class MArgument {
 public:
  enum class ArgumentType : u8 {
    Imm,
    VReg,
    MemVReg,
    MemVRegVReg,
    MemImm,
    MemImmVReg,
    MemImmVRegVReg,
    MemVRegVRegScale,
    MemImmVRegScale,
    MemImmVRegVRegScale,

    Label,
    MemLabel,
    MemImmLabel,
  };

  ArgumentType type;
  Type ty;

  union {
    u64 imm;
    f64 immf;
  };
  u64 scale;
  VReg reg;
  VReg indx;
  IRStringRef label;

  constexpr MArgument() : type(ArgumentType::Imm), imm(0) {}
  // TODO FIX TYPE CONVERSION HERE
  constexpr MArgument(VReg reg, Type ty)
      : type(ArgumentType::VReg), ty(ty), reg(reg) {
    reg.ty = ty;
  }
  constexpr static MArgument Int(u64 val, Type ty) {
    MArgument res;
    res.type = ArgumentType::Imm;
    res.ty = ty;
    res.imm = val;
    return res;
  }
  MArgument(u8 imm) : type(ArgumentType::Imm), ty(Type::Int8), imm(imm) {}
  MArgument(u16 imm) : type(ArgumentType::Imm), ty(Type::Int16), imm(imm) {}
  MArgument(u32 imm) : type(ArgumentType::Imm), ty(Type::Int32), imm(imm) {}
  MArgument(u64 imm) : type(ArgumentType::Imm), ty(Type::Int64), imm(imm) {}
  MArgument(f64 imm) : type(ArgumentType::Imm), ty(Type::Float64), immf(imm) {}
  MArgument(f32 imm)
      : type(ArgumentType::Imm),
        ty(Type::Float32),
        immf(std::bit_cast<f64>((u64)std::bit_cast<u32>(imm))) {}
  MArgument(IRStringRef lab)
      : type(ArgumentType::Label), ty(Type::INVALID), label(lab) {}
  MArgument(IRStringRef lab, Type ty)
      : type(ArgumentType::Label), ty(ty), label(lab) {}

  [[nodiscard]] static constexpr MArgument MemL(IRStringRef lab, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemLabel;
    arg.ty = ty;
    arg.label = lab;
    return arg;
  }

  [[nodiscard]] static constexpr MArgument MemLO(IRStringRef lab, i32 imm,
                                                 Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemImmLabel;
    arg.ty = ty;
    arg.label = lab;
    arg.imm = std::bit_cast<u64>((i64)imm);
    return arg;
  }

  [[nodiscard]] static constexpr MArgument MemO(i32 imm, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemImm;
    arg.ty = ty;
    arg.imm = std::bit_cast<u64>((i64)imm);
    return arg;
  }

  [[nodiscard]] static constexpr MArgument MemB(VReg reg, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemVReg;
    arg.ty = ty;
    arg.reg = reg;
    return arg;
  }

  [[nodiscard]] static constexpr MArgument MemOB(i32 off, VReg reg, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemImmVReg;
    arg.ty = ty;
    arg.imm = std::bit_cast<u64>((i64)off);
    arg.reg = reg;
    return arg;
  }

  [[nodiscard]] static constexpr MArgument MemBI(VReg reg, VReg indx, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemVRegVReg;
    arg.ty = ty;
    arg.reg = reg;
    arg.indx = indx;
    return arg;
  }

  // reg + indx * scale
  [[nodiscard]] static constexpr MArgument MemBIS(VReg reg, VReg indx,
                                                  u32 scale, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemVRegVRegScale;
    arg.ty = ty;
    arg.reg = reg;
    arg.indx = indx;
    arg.scale = scale;
    return arg;
  }

  // reg + indx + off
  [[nodiscard]] static constexpr MArgument MemOBI(i32 off, VReg reg, VReg indx,
                                                  Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemImmVRegVReg;
    arg.ty = ty;
    arg.reg = reg;
    arg.indx = indx;
    arg.imm = std::bit_cast<u64>((i64)off);
    return arg;
  }

  [[nodiscard]] static constexpr MArgument MemOIS(i32 off, VReg indx, u32 scale,
                                                  Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemImmVRegScale;
    arg.ty = ty;
    arg.imm = std::bit_cast<u64>((i64)off);
    arg.indx = indx;
    arg.scale = scale;
    return arg;
  }

  [[nodiscard]] constexpr bool isReg() const {
    return type == ArgumentType::VReg;
  }
  [[nodiscard]] constexpr bool isImm() const {
    return type == ArgumentType::Imm;
  }
  [[nodiscard]] constexpr bool isLabel() const {
    return type == ArgumentType::Label;
  }
  [[nodiscard]] constexpr bool isMem() const {
    switch (type) {
      case ArgumentType::Imm:
      case ArgumentType::VReg:
      case ArgumentType::Label:
        return false;
      case ArgumentType::MemVReg:
      case ArgumentType::MemVRegVReg:
      case ArgumentType::MemImm:
      case ArgumentType::MemImmVReg:
      case ArgumentType::MemImmVRegVReg:
      case ArgumentType::MemVRegVRegScale:
      case ArgumentType::MemImmVRegScale:
      case ArgumentType::MemImmVRegVRegScale:
      case ArgumentType::MemLabel:
      case ArgumentType::MemImmLabel:
        return true;
    }
  }

  constexpr bool operator==(const MArgument &other) const {
    if (type != other.type || ty != other.ty) {
      return false;
    }
    switch (type) {
      case ArgumentType::MemLabel:
      case ArgumentType::Label:
        return label == other.label;
      case ArgumentType::MemImm:
      case ArgumentType::Imm:
        return imm == other.imm;
      case ArgumentType::VReg:
      case ArgumentType::MemVReg:
        return reg == other.reg;
      case ArgumentType::MemImmVReg:
        return reg == other.reg && imm == other.imm;
      case ArgumentType::MemImmVRegScale:
        return indx == other.indx && scale == other.scale && imm == other.imm;
      case ArgumentType::MemVRegVReg:
        return reg == other.reg && indx == other.indx;
      case ArgumentType::MemImmVRegVReg:
        return reg == other.reg && indx == other.indx && imm == other.imm;
      case ArgumentType::MemVRegVRegScale:
        return reg == other.reg && indx == other.indx && scale == other.scale;
      case ArgumentType::MemImmVRegVRegScale:
        return reg == other.reg && indx == other.indx && imm == other.imm &&
               scale == other.scale;
      case ArgumentType::MemImmLabel:
        return label == other.label && imm == other.imm;
    }
  }
  [[nodiscard]] constexpr bool uses_same_vreg(const VReg &other) const {
    switch (type) {
      case ArgumentType::Imm:
      case ArgumentType::MemImm:
      case ArgumentType::Label:
      case ArgumentType::MemLabel:
      case ArgumentType::MemImmLabel:
        return false;
      case ArgumentType::VReg:
      case ArgumentType::MemVReg:
      case ArgumentType::MemImmVReg:
        return reg == other;
      case ArgumentType::MemImmVRegScale:
        return indx == other;
      case ArgumentType::MemImmVRegVReg:
      case ArgumentType::MemVRegVRegScale:
      case ArgumentType::MemVRegVReg:
      case ArgumentType::MemImmVRegVRegScale:
        return reg == other || indx == other;
    }
  }

  [[nodiscard]] constexpr bool uses_same_vreg(const MArgument &other) const {
    switch (other.type) {
      case ArgumentType::Imm:
      case ArgumentType::MemImm:
      case ArgumentType::Label:
      case ArgumentType::MemLabel:
      case ArgumentType::MemImmLabel:
        return false;
      case ArgumentType::VReg:
      case ArgumentType::MemVReg:
      case ArgumentType::MemImmVReg:
        return uses_same_vreg(other.reg);
      case ArgumentType::MemImmVRegScale:
        return uses_same_vreg(other.indx);
      case ArgumentType::MemImmVRegVReg:
      case ArgumentType::MemVRegVRegScale:
      case ArgumentType::MemVRegVReg:
      case ArgumentType::MemImmVRegVRegScale:
        return uses_same_vreg(other.reg) || uses_same_vreg(other.indx);
    }
  }

  [[nodiscard]] constexpr bool uses_vreg() const {
    switch (type) {
      case ArgumentType::MemImmLabel:
      case ArgumentType::Imm:
      case ArgumentType::Label:
      case ArgumentType::MemImm:
      case ArgumentType::MemLabel:
        return false;
      case ArgumentType::VReg:
      case ArgumentType::MemVReg:
      case ArgumentType::MemVRegVReg:
      case ArgumentType::MemImmVReg:
      case ArgumentType::MemImmVRegVReg:
      case ArgumentType::MemVRegVRegScale:
      case ArgumentType::MemImmVRegScale:
      case ArgumentType::MemImmVRegVRegScale:
        return true;
    }
  }

  [[nodiscard]] constexpr bool is_fp() const {
    return ty == Type::Float32 || ty == Type::Float64;
  }
  [[nodiscard]] constexpr bool is_vec_reg() const {
    return ty >= Type::Float32;
  }
};

#define COND_JUMP_GEN(NAME, TYP)                                   \
  static MInstr NAME(MArgument v1, MArgument v2, u32 new_bb_ref) { \
    auto res = MInstr{GOpcode::GJmp, (u32)(TYP), v1, v2};          \
    res.bb_ref = new_bb_ref;                                       \
    res.has_bb_ref = true;                                         \
    return res;                                                    \
  }

#define CONSTR_REGN(TYP, STYP)                                                \
  MInstr(STYP sop) : has_bb_ref(false), n_args(0), bop(TYP), sop((u32)sop) {} \
  MInstr(STYP sop, MArgument a1)                                              \
      : has_bb_ref(false), n_args(1), bop(TYP), sop((u32)sop) {               \
    args[0] = a1;                                                             \
  }                                                                           \
  MInstr(STYP sop, MArgument a1, MArgument a2)                                \
      : has_bb_ref(false), n_args(2), bop(TYP), sop((u32)sop) {               \
    args[0] = a1;                                                             \
    args[1] = a2;                                                             \
  }                                                                           \
  MInstr(STYP sop, MArgument a1, MArgument a2, MArgument a3)                  \
      : has_bb_ref(false), n_args(3), bop(TYP), sop((u32)sop) {               \
    args[0] = a1;                                                             \
    args[1] = a2;                                                             \
    args[2] = a3;                                                             \
  }                                                                           \
  MInstr(STYP sop, MArgument a1, MArgument a2, MArgument a3, MArgument a4)    \
      : has_bb_ref(false), n_args(4), bop(TYP), sop((u32)sop) {               \
    args[0] = a1;                                                             \
    args[1] = a2;                                                             \
    args[2] = a3;                                                             \
    args[3] = a4;                                                             \
  }

class MInstr {
 public:
  u8 has_bb_ref : 1;
  u8 is_var_arg_call : 1 = false;
  u8 n_args;
  GOpcode bop;
  u32 sop;
  u32 bb_ref;
  // TODO: this is quite huge consider moving this into a std::vector or
  // something similar then
  //  we could not only keep that instructions smoller but also could support
  //  var args directly in the instruction + would get rid of extra n_args
  //  member
  MArgument args[4];

  constexpr bool operator==(const MInstr &other) const {
    if (bop != other.bop || sop != other.sop) {
      return false;
    }
    if (has_bb_ref != other.has_bb_ref ||
        is_var_arg_call != other.is_var_arg_call || n_args != other.n_args) {
      return false;
    }
    for (size_t arg_id = 0; arg_id < n_args; arg_id++) {
      if (args[arg_id] != other.args[arg_id]) {
        return false;
      }
    }
    if (has_bb_ref && bb_ref != other.bb_ref) {
      return false;
    }
    return true;
  }

  CONSTR_REGN(GOpcode::GBase, GBaseSubtype);
  CONSTR_REGN(GOpcode::GJmp, GJumpSubtype);
  CONSTR_REGN(GOpcode::GArith, GArithSubtype);
  CONSTR_REGN(GOpcode::GCMov, GCMovSubtype);
  CONSTR_REGN(GOpcode::GConv, GConvSubtype);
  CONSTR_REGN(GOpcode::GVec, GVecSubtype);
  CONSTR_REGN(GOpcode::X86, X86Subtype);

  MInstr(GOpcode op, u32 sop)
      : has_bb_ref(false), n_args(0), bop(op), sop(sop) {}
  MInstr(GOpcode op, u32 sop, MArgument a1)
      : has_bb_ref(false), n_args(1), bop(op), sop(sop) {
    args[0] = a1;
  }
  MInstr(GOpcode op, u32 sop, MArgument a1, MArgument a2)
      : has_bb_ref(false), n_args(2), bop(op), sop(sop) {
    args[0] = a1;
    args[1] = a2;
  }
  MInstr(GOpcode op, u32 sop, MArgument a1, MArgument a2, MArgument a3)
      : has_bb_ref(false), n_args(3), bop(op), sop(sop) {
    args[0] = a1;
    args[1] = a2;
    args[2] = a3;
  }
  MInstr(GOpcode op, u32 sop, MArgument a1, MArgument a2, MArgument a3,
         MArgument a4)
      : has_bb_ref(false), n_args(4), bop(op), sop(sop) {
    args[0] = a1;
    args[1] = a2;
    args[2] = a3;
    args[3] = a4;
  }

  static MInstr jmp(u32 new_bb_ref) {
    auto res = MInstr{GOpcode::GJmp, (u32)GJumpSubtype::jmp};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }

  static MInstr cJmp(MArgument cond, u32 new_bb_ref) {
    auto res = MInstr{GOpcode::GJmp, (u32)GJumpSubtype::cjmp, cond};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }

  [[nodiscard]] bool is(GBaseSubtype sub) const {
    return bop == GOpcode::GBase && sop == (u32)sub;
  }
  [[nodiscard]] bool is(GCMovSubtype sub) const {
    return bop == GOpcode::GCMov && sop == (u32)sub;
  }
  [[nodiscard]] bool is(GConvSubtype sub) const {
    return bop == GOpcode::GConv && sop == (u32)sub;
  }
  [[nodiscard]] bool is(GJumpSubtype sub) const {
    return bop == GOpcode::GJmp && sop == (u32)sub;
  }
  [[nodiscard]] bool is(GVecSubtype sub) const {
    return bop == GOpcode::GVec && sop == (u32)sub;
  }
  [[nodiscard]] bool is(GArithSubtype sub) const {
    return bop == GOpcode::GArith && sop == (u32)sub;
  }

  [[nodiscard]] static bool is_control_flow(GOpcode c, u32 sop);

  COND_JUMP_GEN(cJmp_slt, GJumpSubtype::cjmp_int_slt)
  COND_JUMP_GEN(cJmp_sge, GJumpSubtype::cjmp_int_sge)
  COND_JUMP_GEN(cJmp_sle, GJumpSubtype::cjmp_int_sle)
  COND_JUMP_GEN(cJmp_sgt, GJumpSubtype::cjmp_int_sgt)
  COND_JUMP_GEN(cJmp_ult, GJumpSubtype::cjmp_int_ult)
  COND_JUMP_GEN(cJmp_ule, GJumpSubtype::cjmp_int_ule)
  COND_JUMP_GEN(cJmp_ugt, GJumpSubtype::cjmp_int_ugt)
  COND_JUMP_GEN(cJmp_uge, GJumpSubtype::cjmp_int_uge)
  COND_JUMP_GEN(cJmp_eq, GJumpSubtype::cjmp_int_eq)
  COND_JUMP_GEN(cJmp_ne, GJumpSubtype::cjmp_int_ne)
  COND_JUMP_GEN(cJmp_and, GJumpSubtype::cjmp_and)
  COND_JUMP_GEN(cJmp_or, GJumpSubtype::cjmp_or)

  static MInstr cJmp_flt(MArgument v1, MArgument v2, u32 new_bb_ref,
                         fir::FCmpInstrSubType compare_type) {
    auto sopcode_type = GJumpSubtype::cjmp_flt_oeq;

    switch (compare_type) {
      case fir::FCmpInstrSubType::INVALID:
        UNREACH();
        break;
      case fir::FCmpInstrSubType::AlwFalse:
      case fir::FCmpInstrSubType::AlwTrue:
      case fir::FCmpInstrSubType::IsNaN:
        IMPL("IMPL");
        break;
      case fir::FCmpInstrSubType::OEQ:
        sopcode_type = GJumpSubtype::cjmp_flt_oeq;
        break;
      case fir::FCmpInstrSubType::OGT:
        sopcode_type = GJumpSubtype::cjmp_flt_ogt;
        break;
      case fir::FCmpInstrSubType::OGE:
        sopcode_type = GJumpSubtype::cjmp_flt_oge;
        break;
      case fir::FCmpInstrSubType::OLT:
        sopcode_type = GJumpSubtype::cjmp_flt_olt;
        break;
      case fir::FCmpInstrSubType::OLE:
        sopcode_type = GJumpSubtype::cjmp_flt_ole;
        break;
      case fir::FCmpInstrSubType::ONE:
        sopcode_type = GJumpSubtype::cjmp_flt_one;
        break;
      case fir::FCmpInstrSubType::ORD:
        sopcode_type = GJumpSubtype::cjmp_flt_ord;
        break;
      case fir::FCmpInstrSubType::UNO:
        sopcode_type = GJumpSubtype::cjmp_flt_uno;
        break;
      case fir::FCmpInstrSubType::UEQ:
        sopcode_type = GJumpSubtype::cjmp_flt_ueq;
        break;
      case fir::FCmpInstrSubType::UGT:
        sopcode_type = GJumpSubtype::cjmp_flt_ugt;
        break;
      case fir::FCmpInstrSubType::UGE:
        sopcode_type = GJumpSubtype::cjmp_flt_uge;
        break;
      case fir::FCmpInstrSubType::ULT:
        sopcode_type = GJumpSubtype::cjmp_flt_ult;
        break;
      case fir::FCmpInstrSubType::ULE:
        sopcode_type = GJumpSubtype::cjmp_flt_ule;
        break;
      case fir::FCmpInstrSubType::UNE:
        sopcode_type = GJumpSubtype::cjmp_flt_une;
        break;
    }

    auto res = MInstr{GOpcode::GJmp, (u32)sopcode_type, v1, v2};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }
};
#undef COND_JUMP_GEN

struct ArgData {
  u8 indx;
  MArgument arg;
};

void written_args(const MInstr &instr, TVec<ArgData> &out);
void read_args(const MInstr &instr, TVec<ArgData> &out);

bool verify(const FVec<MFunc> &funcs);
bool verify(const MFunc &funcs);

}  // namespace foptim::fmir

template <>
struct std::hash<foptim::fmir::VReg> {
  std::size_t operator()(const foptim::fmir::VReg &k) const {
    using foptim::u64;
    using foptim::u8;
    if (k.is_concrete()) {
      return hash<u8>()((u8)k.c_reg());
    }
    return hash<u64>()(k.virt_id());
  }
};
