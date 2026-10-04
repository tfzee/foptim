#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/types_ref.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "utils/vec.hpp"

namespace foptim::fir {

enum class InstrType : u8 {
  ICmp,
  FCmp,
  BinaryInstr,
  UnaryInstr,

  // Structs / Vectors
  ExtractValue,
  InsertValue,
  VectorInstr,

  // Conversions
  ITrunc,
  ZExt,
  SExt,
  Conversion,

  SelectInstr,

  CallInstr,
  // Terminators
  ReturnInstr,
  BranchInstr,
  CondBranchInstr,
  SwitchInstr,
  Unreachable,

  // Memory
  AllocaInstr,
  LoadInstr,
  StoreInstr,
  AtomicRMW,
  Fence,

  // Intrinsic
  Intrinsic,
};

struct BBRefWithArgs {
  BasicBlock bb;
  IRVec<ValueR> args;

  bool operator==(const BBRefWithArgs &other) const;
};

enum class AtomicRMWSubType : u32 {
  INVALID = 0,
  Add,
  Xchg,
  Or,
};

enum class VectorISubType : u32 {
  INVALID = 0,
  Broadcast,
  Shuffle,
  Concat,
  ExtractHigh,
  ExtractLow,
  HorizontalAdd,
  HorizontalMul,
};

enum class IntrinsicSubType : u32 {
  INVALID = 0,
  // count leading zeroes
  CTLZ,
  CTTZ,
  VA_start,
  VA_end,
  Abs,
  FAbs,
  UMin,
  UMax,
  SMin,
  SMax,
  // like std and llvm.maxinum7minnum
  FMin,
  FMax,
  // like llvm.maximum
  FMinimum,
  FMaximum,

  PopCnt,

  FRound,
  FCeil,
  FFloor,
  FTrunc,

  IsConstant,

  // ptr, val, u64 n_elements
  Memset,
  // ptr, ptr, u64 bytes
  Memcpy,
};

enum class ConversionSubType : u32 {
  INVALID = 0,
  FPEXT,
  FPTRUNC,
  FPTOUI,
  FPTOSI,
  UITOFP,
  SITOFP,
  PtrToInt,
  IntToPtr,
  BitCast,
};

enum class ICmpInstrSubType : u32 {
  INVALID = 0,
  SLT,
  ULT,
  NE,
  EQ,
  SGT,
  UGT,
  UGE,
  ULE,
  SGE,
  SLE,

  MulOverflow,
  AddOverflow,
};

enum class FCmpInstrSubType : u32 {
  INVALID = 0,
  AlwFalse,
  OEQ,
  OGT,
  OGE,
  OLT,
  OLE,
  ONE,
  ORD,
  UNO,
  UEQ,
  UGT,
  UGE,
  ULT,
  ULE,
  UNE,
  AlwTrue,

  IsNaN,
};

enum class UnaryInstrSubType : u32 {
  INVALID = 0,
  FloatNeg,
  IntNeg,
  Not,
  FloatSqrt,
};

enum class BinaryInstrSubType : u32 {
  INVALID = 0,
  PtrAdd,

  IntAdd,
  IntSub,
  IntMul,
  IntSRem,
  IntURem,
  IntSDiv,
  IntUDiv,

  Shl,
  Shr,
  AShr,
  And,
  Or,
  Xor,

  FloatAdd,
  FloatSub,
  FloatMul,
  FloatDiv,
};

enum Ordering : u8 {
  NonAtomic = 0,
  Unorderd = 1,
  Monotone = 2,
  Acquire = 3,
  Release = 4,
  Acq_Rel = 5,
  Seq_Cst = 6,
};

struct InstrAttribs {
  fir::TypeR extra_type{fir::TypeR::invalid()};
  u8 NSW : 1 = 0;
  u8 NUW : 1 = 0;
  u8 InBounds : 1 = 0;
  u8 Volatile : 1 = 0;
  u8 Atomic : 1 = 0;
  u8 Ordering : 3 = 0;
};

class InstrData : public Used, public InstrAttribs {
public:
  InstrType instr_type;
  u32 subtype;
  TypeR value_type;

  IRVec<ValueR> args;
  IRVec<BBRefWithArgs> bbs;
  BasicBlock parent;

  InstrData(InstrType ty, TypeR vty, BasicBlock parent)
      : instr_type(ty), subtype(0), value_type(vty), parent(parent) {}
  InstrData(InstrType ty, u32 subtype, TypeR vty, BasicBlock parent)
      : instr_type(ty), subtype(subtype), value_type(vty), parent(parent) {}

  using Used::add_usage;
  [[nodiscard]] constexpr InstrType get_instr_type() const {
    return instr_type;
  }
  [[nodiscard]] constexpr u32 get_instr_subtype() const { return subtype; }
  constexpr void set_parent(BasicBlock new_parent) { parent = new_parent; }
  [[nodiscard]] constexpr BasicBlock get_parent() const { return parent; }

  [[nodiscard]] constexpr const auto &get_uses() const { return uses; }
  [[nodiscard]] constexpr const auto &get_args() const { return args; }
  [[nodiscard]] constexpr size_t get_n_args() const { return args.size(); }
  [[nodiscard]] constexpr const ValueR &get_arg(size_t indx) const {
    return args.at(indx);
  }
  [[nodiscard]] constexpr bool has_args() const { return !args.empty(); }
  [[nodiscard]] constexpr const auto &get_bb_args() const { return bbs; }
  [[nodiscard]] constexpr TypeR get_type() const { return value_type; }
  [[nodiscard]] bool verify(const BasicBlockData *) const;
  [[nodiscard]] bool has_result() const;
  [[nodiscard]] bool is_critical() const;
  /*true if the instructions arguments can be swapped without changing the
   * result*/
  [[nodiscard]] bool is_commutative() const;
  [[nodiscard]] bool pot_modifies_mem() const;
  [[nodiscard]] bool pot_reads_mem() const;
  [[nodiscard]] bool has_pot_sideeffects() const;

  [[nodiscard]] const char *get_name() const;
  [[nodiscard]] constexpr bool is(InstrType ty) const {
    return ty == instr_type;
  }

  [[nodiscard]] constexpr bool is(ICmpInstrSubType ty) const {
    return instr_type == InstrType::ICmp && subtype == static_cast<u32>(ty);
  }

  [[nodiscard]] constexpr bool is(ConversionSubType ty) const {
    return instr_type == InstrType::Conversion &&
           subtype == static_cast<u32>(ty);
  }

  [[nodiscard]] constexpr bool is(FCmpInstrSubType ty) const {
    return instr_type == InstrType::FCmp && subtype == static_cast<u32>(ty);
  }

  [[nodiscard]] constexpr bool is(BinaryInstrSubType ty) const {
    return instr_type == InstrType::BinaryInstr &&
           subtype == static_cast<u32>(ty);
  }

  [[nodiscard]] constexpr bool is(UnaryInstrSubType ty) const {
    return instr_type == InstrType::UnaryInstr &&
           subtype == static_cast<u32>(ty);
  }

  [[nodiscard]] constexpr bool is(VectorISubType ty) const {
    return instr_type == InstrType::VectorInstr &&
           subtype == static_cast<u32>(ty);
  }

  [[nodiscard]] constexpr bool is(IntrinsicSubType ty) const {
    return instr_type == InstrType::Intrinsic &&
           subtype == static_cast<u32>(ty);
  }

  constexpr void verify() const {
    switch (instr_type) {
    case InstrType::BinaryInstr:
      ASSERT(args.size() == 2);
      break;
    case InstrType::ICmp:
      ASSERT(args.size() == 2);
      break;
    case InstrType::CallInstr:
      ASSERT(args.size() >= 1);
      break;
    case InstrType::AllocaInstr:
      ASSERT(args.size() == 1);
      break;
    case InstrType::ReturnInstr:
      ASSERT(args.size() <= 1);
      break;
    case InstrType::LoadInstr:
      ASSERT(args.size() == 1);
      break;
    case InstrType::StoreInstr:
      ASSERT(args.size() == 2);
      break;
    default:
      break;
    }
  }

  static InstrData get_extract_value(TypeR ty);
  static InstrData get_insert_value(TypeR ty);
  static InstrData get_smod(TypeR ty);
  static InstrData get_umod(TypeR ty);
  static InstrData get_intrinsic(TypeR ty, IntrinsicSubType sub_type);
  static InstrData get_vector(TypeR ty, VectorISubType sub_type);
  static InstrData get_add(TypeR ty);
  static InstrData get_sub(TypeR ty);
  static InstrData get_mul(TypeR ty);
  static InstrData get_shl(TypeR ty);
  static InstrData get_ashr(TypeR ty);
  static InstrData get_lshr(TypeR ty);
  static InstrData get_binary(TypeR ty, BinaryInstrSubType sub_type);
  static InstrData get_unary(TypeR ty, UnaryInstrSubType sub_type);
  static InstrData get_conversion(TypeR ty, ConversionSubType sub_type);
  static InstrData get_float_add(TypeR ty);
  static InstrData get_float_sub(TypeR ty);
  static InstrData get_float_mul(TypeR ty);
  static InstrData get_float_div(TypeR ty);
  static InstrData get_sext(TypeR ty);
  static InstrData get_itrunc(TypeR ty);
  static InstrData get_zext(TypeR ty);
  static InstrData get_atomic_rmw(TypeR ty, AtomicRMWSubType sub_ty);
  static InstrData get_fence(TypeR ty);
  static InstrData get_int_cmp(TypeR ty, ICmpInstrSubType cmp_ty);
  static InstrData get_float_cmp(TypeR ty, FCmpInstrSubType cmp_ty);
  // only give void  type
  static InstrData get_unreach(TypeR ty);
  static InstrData get_return(TypeR ty);
  static InstrData get_call(TypeR ty);
  static InstrData get_alloca(TypeR ty);
  static InstrData get_load(TypeR ty);
  static InstrData get_select(TypeR ty);
  static InstrData get_store(TypeR ty);
  static InstrData get_branch(ContextData *ctx);
  static InstrData get_switch(ContextData *ctx);
  static InstrData get_cond_branch(ContextData *ctx);

  [[nodiscard]] bool eql_expr(const InstrData &other) const;
};

} // namespace foptim::fir
