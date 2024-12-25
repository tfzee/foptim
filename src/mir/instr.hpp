#pragma once
#include "ir/instruction_data.hpp"
#include "utils/logging.hpp"
#include "utils/todo.hpp"
#include "utils/types.hpp"
#include <string>
#include <utility>

namespace foptim::fmir {

enum class Opcode : u32 {
  mov,
  cmov,
  mov_zx,
  mov_sx,
  itrunc,
  lea,

  shl,
  shr,
  sar,
  land,
  lor,
  lxor,

  add,
  sub,
  mul,
  idiv,
  fadd,
  fsub,
  fmul,
  fdiv,
  ffmadd132,
  ffmadd213,
  ffmadd231,

  SI2FL,
  UI2FL,
  FL2SI,
  FL2UI,

  push,
  pop,

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

#define ReturnString(name)                                                     \
  case Opcode::name:                                                           \
    return #name;

constexpr const char *getNameFromOpcode(Opcode code) {
  switch (code) {
    ReturnString(icmp_ult);
    ReturnString(icmp_ne);
    ReturnString(icmp_sgt);
    ReturnString(icmp_ugt);
    ReturnString(icmp_uge);
    ReturnString(icmp_ule);
    ReturnString(icmp_sge);
    ReturnString(icmp_sle);
    ReturnString(mov);
    ReturnString(cmov);
    ReturnString(lea);
    ReturnString(itrunc);
    ReturnString(mov_zx);
    ReturnString(mov_sx);
    ReturnString(idiv);
    ReturnString(add);
    ReturnString(sub);
    ReturnString(mul);
    ReturnString(fadd);
    ReturnString(fsub);
    ReturnString(fmul);
    ReturnString(ffmadd132);
    ReturnString(ffmadd231);
    ReturnString(ffmadd213);
    ReturnString(icmp_slt);
    ReturnString(icmp_eq);
    ReturnString(jmp);
    ReturnString(call);
    ReturnString(push);
    ReturnString(pop);
    ReturnString(cjmp);
    ReturnString(ret);
    ReturnString(arg_setup);
    ReturnString(invoke);
    ReturnString(cjmp_int_slt);
    ReturnString(cjmp_int_sge);
    ReturnString(cjmp_int_sle);
    ReturnString(cjmp_int_sgt);
    ReturnString(cjmp_int_ne);
    ReturnString(cjmp_int_eq);
    ReturnString(cjmp_int_ult);
    ReturnString(cjmp_int_ule);
    ReturnString(cjmp_int_uge);
    ReturnString(cjmp_int_ugt);
    ReturnString(cjmp_flt_oeq);
    ReturnString(cjmp_flt_ogt);
    ReturnString(cjmp_flt_oge);
    ReturnString(cjmp_flt_olt);
    ReturnString(cjmp_flt_ole);
    ReturnString(cjmp_flt_one);
    ReturnString(cjmp_flt_ord);
    ReturnString(cjmp_flt_uno);
    ReturnString(cjmp_flt_ueq);
    ReturnString(cjmp_flt_ugt);
    ReturnString(cjmp_flt_uge);
    ReturnString(cjmp_flt_ult);
    ReturnString(cjmp_flt_ule);
    ReturnString(cjmp_flt_une);
    ReturnString(shl);
    ReturnString(shr);
    ReturnString(sar);
    ReturnString(land);
    ReturnString(lor);
    ReturnString(lxor);
    ReturnString(fdiv);
    ReturnString(SI2FL);
    ReturnString(UI2FL);
    ReturnString(FL2SI);
    ReturnString(FL2UI);
  }
}
#undef ReturnString

enum class Type : u16 {
  INVALID = 0,
  Int8 = 1,
  Int16 = 2,
  Int32 = 3,
  Int64 = 4,

  Float32 = 5,
  Float64 = 6,

};

/*Returns the size in bytes of the given type*/
static constexpr u32 get_size(fmir::Type type) {
  switch (type) {
  case fmir::Type::Float32:
    return 4;
  case fmir::Type::Float64:
    return 8;
  case fmir::Type::Int8:
    return 1;
  case fmir::Type::Int16:
    return 2;
  case fmir::Type::Int32:
    return 4;
  case fmir::Type::Int64:
    return 8;
  case fmir::Type::INVALID:
    TODO("INVALID TYPE");
  }
  ASSERT(false);
  std::abort();
}

enum class VRegType : u8 {
  Virtual = 0,
  A = 1,
  B,
  C,
  D,
  SI,
  DI,
  SP,
  BP,
  R8,
  R9,
  R10,
  R11,
  R12,
  R13,
  R14,
  R15 = 16,
  mm0 = 17,
  mm1,
  mm2,
  mm3,
  mm4,
  mm5,
  mm6,
  mm7,
  mm8,
  mm9,
  mm10,
  mm11,
  mm12,
  mm13,
  mm14,
  mm15 = 32,
  // TODO: support the other registers that come with avx so 16 -> 31
  N_REGS,
};

enum class VRegClass {
  INVALID = 0,
  GeneralPurpose,
  Float,
  // Vector,
};

struct VRegInfo {
  VRegType ty;
  u8 reg_size;
  VRegClass reg_class;

  constexpr VRegInfo()
      : ty(VRegType::Virtual), reg_size(0), reg_class(VRegClass::INVALID) {}
  constexpr VRegInfo(VRegClass reg_class)
      : ty(VRegType::Virtual), reg_size(0), reg_class(reg_class) {}
  // explicit constexpr VRegInfo(u8 size)
  //     : ty(VRegType::Virtual), reg_size(size),
  //       reg_class(VRegClass::GeneralPurpose) {}
  explicit constexpr VRegInfo(u8 size, VRegClass reg_class)
      : ty(VRegType::Virtual), reg_size(size), reg_class(reg_class) {}

  explicit constexpr VRegInfo(Type type)
      : ty(VRegType::Virtual), reg_size(get_size(type)),
        reg_class(VRegClass::GeneralPurpose) {
    if (type == Type::Float32 || type == Type::Float64) {
      reg_class = VRegClass::Float;
    } else if (type == Type::INVALID) {
      reg_class = VRegClass::INVALID;
    }
  }
  // explicit constexpr VRegInfo(VRegType ty, u8 size)
  //     : ty(ty), reg_size(size), reg_class(VRegClass::GeneralPurpose) {}
  explicit constexpr VRegInfo(VRegType ty, Type type)
      : ty(ty), reg_size(get_size(type)), reg_class(VRegClass::GeneralPurpose) {
    if (type == Type::Float32 || type == Type::Float64) {
      reg_class = VRegClass::Float;
    } else if (type == Type::INVALID) {
      reg_class = VRegClass::INVALID;
    }
  }

  static constexpr VRegInfo RSP() {
    VRegInfo res{Type::Int64};
    res.ty = VRegType::SP;
    return res;
  }

  static constexpr VRegInfo ESP() {
    VRegInfo res{Type::Int32};
    res.ty = VRegType::SP;
    return res;
  }

  static constexpr VRegInfo RAX() {
    VRegInfo res{Type::Int64};
    res.ty = VRegType::A;
    return res;
  }

  static constexpr VRegInfo EAX() {
    VRegInfo res{Type::Int32};
    res.ty = VRegType::A;
    return res;
  }

  static constexpr VRegInfo CL() {
    VRegInfo res{Type::Int8};
    res.ty = VRegType::C;
    return res;
  }

  static constexpr VRegInfo EDX() {
    VRegInfo res{Type::Int32};
    res.ty = VRegType::D;
    return res;
  }

  [[nodiscard]] constexpr bool is_pinned() const {
    return ty != VRegType::Virtual;
  }

  constexpr bool operator==(const VRegInfo &other) const {
    return ty == other.ty && reg_class == other.reg_class;
  }

  [[nodiscard]] constexpr bool isVecReg() const {
    switch (ty) {
    case VRegType::Virtual:
    case VRegType::A:
    case VRegType::B:
    case VRegType::C:
    case VRegType::D:
    case VRegType::SI:
    case VRegType::DI:
    case VRegType::SP:
    case VRegType::BP:
    case VRegType::R8:
    case VRegType::R9:
    case VRegType::R10:
    case VRegType::R11:
    case VRegType::R12:
    case VRegType::R13:
    case VRegType::R14:
    case VRegType::R15:
    case VRegType::N_REGS:
      return false;
    case VRegType::mm0:
    case VRegType::mm1:
    case VRegType::mm2:
    case VRegType::mm3:
    case VRegType::mm4:
    case VRegType::mm5:
    case VRegType::mm6:
    case VRegType::mm7:
    case VRegType::mm8:
    case VRegType::mm9:
    case VRegType::mm10:
    case VRegType::mm11:
    case VRegType::mm12:
    case VRegType::mm13:
    case VRegType::mm14:
    case VRegType::mm15:
      return true;
    }
  }
};

class VReg {
public:
  size_t id;
  VRegInfo info;

  constexpr VReg() : id(0) {}
  constexpr VReg(u64 id) : id(id) {}
  constexpr VReg(u64 id, VRegInfo info) : id(id), info(info) {}
  constexpr VReg(u64 id, u8 size, VRegClass reg_class)
      : id(id), info(VRegInfo{size, reg_class}) {}
  constexpr VReg(u64 id, Type ty) : id(id), info(ty) {}
  constexpr VReg(VRegType ty, Type typ = Type::Int8)
      : id(0), info(VRegInfo{ty, typ}) {}

  static constexpr VReg EAX() {
    VRegInfo res_info{};
    res_info.ty = VRegType::A;
    res_info.reg_size = 4;
    VReg res{0, res_info};
    return res;
  }

  static constexpr VReg RSP() {
    VRegInfo res_info{};
    res_info.ty = VRegType::SP;
    res_info.reg_size = 8;
    VReg res{0, res_info};
    return res;
  }

  constexpr bool operator==(const VReg &other) const {
    const bool info_matches = info == other.info;
    if (info.ty == VRegType::Virtual) {
      return id == other.id && info_matches;
    }
    return info_matches;
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
  IRString label;

  constexpr MArgument() : type(ArgumentType::Imm), imm(0) {}
  // TODO FIX TYPE CONVERSION HERE
  constexpr MArgument(VReg reg, Type ty)
      : type(ArgumentType::VReg), ty(ty), reg(reg) {
    reg.info.reg_size = get_size(ty);
  }
  MArgument(u8 imm) : type(ArgumentType::Imm), ty(Type::Int8), imm(imm) {}
  MArgument(u16 imm) : type(ArgumentType::Imm), ty(Type::Int16), imm(imm) {}
  MArgument(u32 imm) : type(ArgumentType::Imm), ty(Type::Int32), imm(imm) {}
  MArgument(u64 imm) : type(ArgumentType::Imm), ty(Type::Int64), imm(imm) {}
  MArgument(f64 imm) : type(ArgumentType::Imm), ty(Type::Float64), immf(imm) {}
  MArgument(f32 imm) : type(ArgumentType::Imm), ty(Type::Float32), immf(imm) {}
  MArgument(IRString lab)
      : type(ArgumentType::Label), ty(Type::INVALID), label(std::move(lab)) {}
  MArgument(IRString lab, Type ty)
      : type(ArgumentType::Label), ty(ty), label(std::move(lab)) {}

  [[nodiscard]] static constexpr MArgument Mem(IRString lab, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemLabel;
    arg.ty = ty;
    arg.label = lab;
    return arg;
  }

  [[nodiscard]] static constexpr MArgument Mem(IRString lab, u32 imm,
                                               Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemImmLabel;
    arg.ty = ty;
    arg.label = lab;
    arg.imm = imm;
    return arg;
  }

  [[nodiscard]] static constexpr MArgument Mem(VReg reg, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemVReg;
    arg.ty = ty;
    arg.reg = reg;
    return arg;
  }

  [[nodiscard]] static constexpr MArgument Mem(VReg reg, u32 off, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemImmVReg;
    arg.ty = ty;
    arg.imm = off;
    arg.reg = reg;
    return arg;
  }

  [[nodiscard]] static constexpr MArgument Mem(VReg reg, VReg indx, Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemVRegVReg;
    arg.ty = ty;
    arg.reg = reg;
    arg.indx = indx;
    return arg;
  }

  [[nodiscard]] static constexpr MArgument Mem(VReg reg, VReg indx, u32 scale,
                                               Type ty) {
    MArgument arg;
    arg.type = ArgumentType::MemVRegVRegScale;
    arg.ty = ty;
    arg.reg = reg;
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
    case ArgumentType::MemVRegVReg:
    case ArgumentType::MemImmVReg:
    case ArgumentType::MemImmVRegVReg:
    case ArgumentType::MemVRegVRegScale:
    case ArgumentType::MemImmVRegScale:
    case ArgumentType::MemImmVRegVRegScale:
    case ArgumentType::MemImmLabel:
      TODO("impl");
      break;
      break;
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
};

#define COND_JUMP_GEN(NAME, TYP)                                               \
  static MInstr NAME(MArgument v1, MArgument v2, u32 new_bb_ref) {             \
    auto res = MInstr{TYP, v1, v2};                                            \
    res.bb_ref = new_bb_ref;                                                   \
    res.has_bb_ref = true;                                                     \
    return res;                                                                \
  }

class MInstr {
public:
  bool has_bb_ref;
  u8 n_args;
  Opcode op;
  u32 bb_ref;
  MArgument args[4];

  MInstr(Opcode op) : has_bb_ref(false), n_args(0), op(op) {}
  MInstr(Opcode op, MArgument a1) : has_bb_ref(false), n_args(1), op(op) {
    args[0] = a1;
  }
  MInstr(Opcode op, MArgument a1, MArgument a2)
      : has_bb_ref(false), n_args(2), op(op) {
    args[0] = a1;
    args[1] = a2;
  }
  MInstr(Opcode op, MArgument a1, MArgument a2, MArgument a3)
      : has_bb_ref(false), n_args(3), op(op) {
    args[0] = a1;
    args[1] = a2;
    args[2] = a3;
  }
  MInstr(Opcode op, MArgument a1, MArgument a2, MArgument a3, MArgument a4)
      : has_bb_ref(false), n_args(4), op(op) {
    args[0] = a1;
    args[1] = a2;
    args[2] = a3;
    args[3] = a4;
  }

  static MInstr jmp(u32 new_bb_ref) {
    auto res = MInstr{Opcode::jmp};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }

  static MInstr cJmp(MArgument cond, u32 new_bb_ref) {
    auto res = MInstr{Opcode::cjmp, cond};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }

  COND_JUMP_GEN(cJmp_slt, Opcode::cjmp_int_slt)
  COND_JUMP_GEN(cJmp_sge, Opcode::cjmp_int_sge)
  COND_JUMP_GEN(cJmp_sle, Opcode::cjmp_int_sle)
  COND_JUMP_GEN(cJmp_sgt, Opcode::cjmp_int_sgt)
  COND_JUMP_GEN(cJmp_ult, Opcode::cjmp_int_ult)
  COND_JUMP_GEN(cJmp_ule, Opcode::cjmp_int_ule)
  COND_JUMP_GEN(cJmp_ugt, Opcode::cjmp_int_ugt)
  COND_JUMP_GEN(cJmp_uge, Opcode::cjmp_int_uge)
  COND_JUMP_GEN(cJmp_eq, Opcode::cjmp_int_eq)
  COND_JUMP_GEN(cJmp_ne, Opcode::cjmp_int_ne)

  static MInstr cJmp_flt(MArgument v1, MArgument v2, u32 new_bb_ref,
                         fir::FCmpInstrSubType compare_type) {
    auto opcode_type = Opcode::cjmp_flt_oeq;

    switch (compare_type) {
    case fir::FCmpInstrSubType::INVALID:
      UNREACH();
      break;
    case fir::FCmpInstrSubType::AlwFalse:
    case fir::FCmpInstrSubType::AlwTrue:
      IMPL("IMPL");
      break;
    case fir::FCmpInstrSubType::OEQ:
      opcode_type = Opcode::cjmp_flt_oeq;
      break;
    case fir::FCmpInstrSubType::OGT:
      opcode_type = Opcode::cjmp_flt_ogt;
      break;
    case fir::FCmpInstrSubType::OGE:
      opcode_type = Opcode::cjmp_flt_oge;
      break;
    case fir::FCmpInstrSubType::OLT:
      opcode_type = Opcode::cjmp_flt_olt;
      break;
    case fir::FCmpInstrSubType::OLE:
      opcode_type = Opcode::cjmp_flt_ole;
      break;
    case fir::FCmpInstrSubType::ONE:
      opcode_type = Opcode::cjmp_flt_one;
      break;
    case fir::FCmpInstrSubType::ORD:
      opcode_type = Opcode::cjmp_flt_ord;
      break;
    case fir::FCmpInstrSubType::UNO:
      opcode_type = Opcode::cjmp_flt_uno;
      break;
    case fir::FCmpInstrSubType::UEQ:
      opcode_type = Opcode::cjmp_flt_ueq;
      break;
    case fir::FCmpInstrSubType::UGT:
      opcode_type = Opcode::cjmp_flt_ugt;
      break;
    case fir::FCmpInstrSubType::UGE:
      opcode_type = Opcode::cjmp_flt_uge;
      break;
    case fir::FCmpInstrSubType::ULT:
      opcode_type = Opcode::cjmp_flt_ult;
      break;
    case fir::FCmpInstrSubType::ULE:
      opcode_type = Opcode::cjmp_flt_ule;
      break;
    case fir::FCmpInstrSubType::UNE:
      opcode_type = Opcode::cjmp_flt_une;
      break;
    }

    auto res = MInstr{opcode_type, v1, v2};
    res.bb_ref = new_bb_ref;
    res.has_bb_ref = true;
    return res;
  }
};
#undef COND_JUMP_GEN

} // namespace foptim::fmir

template <> struct std::hash<foptim::fmir::VReg> {
  std::size_t operator()(const foptim::fmir::VReg &k) const {
    using foptim::u64;
    using foptim::u8;
    if (k.info.is_pinned()) {
      return hash<u8>()((u8)k.info.ty);
    }
    return hash<u64>()(k.id);
  }
};
