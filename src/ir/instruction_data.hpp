#pragma once
#include <span>

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
  ExtractElement,
  InsertElement,
  VectorInstr,
  // horizontal reduction of all lanes of a vector, the subtype is the operation
  ReduceInstr,

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

// The branch targets of a terminator, allocated on first use since almost no
// instruction has any. The same pointer slot holds the result types of a call
// with several results (tagged in the lowest bit), a call never has targets.
class BBList {
  using Vec = IRVec<BBRefWithArgs>;
  static constexpr uintptr_t TypesTag = 1;
  uintptr_t bits_ = 0;

  [[nodiscard]] bool holds_types() const { return (bits_ & TypesTag) != 0; }
  [[nodiscard]] static const Vec &empty_vec() {
    static const Vec empty;
    return empty;
  }
  // null when there are no targets
  [[nodiscard]] const Vec *vec() const {
    return holds_types() ? nullptr : reinterpret_cast<const Vec *>(bits_);
  }
  [[nodiscard]] const Vec &vec_or_empty() const {
    const Vec *v = vec();
    return v == nullptr ? empty_vec() : *v;
  }
  Vec &vec_mut() {
    ASSERT(!holds_types());
    if (bits_ == 0) {
      bits_ = reinterpret_cast<uintptr_t>(
          new (utils::IRAlloc<Vec>().allocate(1)) Vec());
    }
    return *reinterpret_cast<Vec *>(bits_);
  }

 public:
  using iterator = Vec::iterator;
  using const_iterator = Vec::const_iterator;
  BBList() = default;
  BBList(const BBList &other) : bits_(other.bits_) {
    if (other.vec() != nullptr) {
      bits_ = reinterpret_cast<uintptr_t>(
          new (utils::IRAlloc<Vec>().allocate(1)) Vec(*other.vec()));
    }
  }
  BBList &operator=(const BBList &other) {
    if (this != &other) {
      bits_ = BBList(other).bits_;
    }
    return *this;
  }

  // result types of a call, immutable array of n entries
  void set_types(TypeR *types) {
    ASSERT(bits_ == 0 || holds_types());
    bits_ = reinterpret_cast<uintptr_t>(types) | TypesTag;
  }
  void clear_types() {
    ASSERT(bits_ == 0 || holds_types());
    bits_ = 0;
  }
  [[nodiscard]] const TypeR *types() const {
    ASSERT(holds_types());
    return reinterpret_cast<const TypeR *>(bits_ & ~TypesTag);
  }

  [[nodiscard]] size_t size() const { return vec_or_empty().size(); }
  [[nodiscard]] bool empty() const { return size() == 0; }
  [[nodiscard]] const BBRefWithArgs &operator[](size_t i) const {
    return vec_or_empty()[i];
  }
  BBRefWithArgs &operator[](size_t i) { return vec_mut()[i]; }
  [[nodiscard]] const BBRefWithArgs &at(size_t i) const {
    return vec_or_empty().at(i);
  }
  [[nodiscard]] const BBRefWithArgs &back() const {
    return vec_or_empty().back();
  }
  BBRefWithArgs &back() { return vec_mut().back(); }
  [[nodiscard]] const_iterator begin() const { return vec_or_empty().begin(); }
  [[nodiscard]] const_iterator end() const { return vec_or_empty().end(); }
  iterator begin() { return vec() == nullptr ? iterator{} : vec_mut().begin(); }
  iterator end() { return vec() == nullptr ? iterator{} : vec_mut().end(); }
  void push_back(BBRefWithArgs v) { vec_mut().push_back(std::move(v)); }
  template <class... A>
  BBRefWithArgs &emplace_back(A &&...a) {
    return vec_mut().emplace_back(std::forward<A>(a)...);
  }
  void reserve(size_t n) { vec_mut().reserve(n); }
  void clear() {
    if (vec() != nullptr) {
      vec_mut().clear();
    }
  }
  iterator erase(const_iterator it) { return vec_mut().erase(it); }
  iterator insert(const_iterator it, BBRefWithArgs v) {
    return vec_mut().insert(it, std::move(v));
  }
  operator const Vec &() const { return vec_or_empty(); }
};

// args are (ptr, value), the result is the old value. CmpXchg takes
// (ptr, expected, new) and stored new iff the old value equals expected.
enum class AtomicRMWSubType : u16 {
  INVALID = 0,
  Add,
  Sub,
  Xchg,
  And,
  Or,
  Xor,
  Nand,
  Max,
  Min,
  UMax,
  UMin,
  CmpXchg,
};

enum class VectorISubType : u16 {
  INVALID = 0,
  Broadcast,
  Shuffle,
  Concat,
  ExtractHigh,
  ExtractLow,
  // (src, control) both byte vectors: result byte i is zero when
  // control byte i has bit 7 set, else the byte (control & 15) of the 16 byte
  // lane of src that holds i (pshufb)
  ByteShuffle,
};

// The result is the scalar combination of all lanes of the operand in any
// order, so only operations that are associative and commutative on the lanes.
enum class ReduceInstrSubType : u16 {
  INVALID = 0,
  Add,
  Mul,
  And,
  Or,
  Xor,
  SMin,
  SMax,
  UMin,
  UMax,
  FMin,
  FMax,
};

enum class IntrinsicSubType : u16 {
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
  // reverses the bytes of an integer (llvm.bswap), widths 16, 32, 64
  Bswap,

  FRound,
  FCeil,
  FFloor,
  FTrunc,
  // Fma(a, b, c): a * b + c with a single rounding (llvm.fma), floats and
  // float vectors
  Fma,
  // Clmul(a, b, imm): carry less multiplication of one 64 bit half of each
  // <2 x i64> operand (pclmulqdq), bit 0 of the constant i8 imm selects the
  // half of a, bit 4 the half of b, the result is the 128 bit product
  Clmul,
  // The x86 vector operations below have no generic form. They only exist in
  // the 128 and 256 bit versions of the llvm.x86 intrinsics of the same name.
  // PackSS(a, b) / PackUS(a, b): the lanes of a and b (16 or 32 bit signed)
  // saturate to signed / unsigned lanes of half the width, per 128 bit lane
  // the result has the lanes of a followed by those of b (packsswb, packuswb,
  // packssdw, packusdw)
  PackSS,
  PackUS,
  // PMulHW(a, b): high 16 bits of the signed products of 16 bit lanes
  PMulHW,
  // PMaddWD(a, b): <2n x i16> operands, lane i of the <n x i32> result is
  // a[2i] * b[2i] + a[2i+1] * b[2i+1] (signed)
  PMaddWD,
  // PMaddUBSW(a, b): <2n x i8> operands (a unsigned, b signed), lane i of the
  // <n x i16> result is the signed saturated sum of the two products
  PMaddUBSW,
  // PermD(a, idx): <8 x i32>, lane i of the result is a[idx[i] & 7] (vpermd)
  PermD,
  // MoveMask(v): i32 whose bit i is the sign bit of lane i of v (movmskps,
  // movmskpd, pmovmskb by lane width)
  MoveMask,

  IsConstant,

  // Memset(dst, val, count): stores val (any sized type, a byte for the C
  // memset) count times in a row starting at dst, so it writes count * size of
  // val bytes. Volatile applies to every store. Has no result, the backend can
  // not emit it so IntrinExpand turns what is left into a loop.
  Memset,
  // Memcpy(dst, src, count): copies count elements of extra_type (bytes when
  // extra_type is not set) from src to dst, the regions must not overlap.
  // The element type can be any sized type, so the copied byte size is
  // count * extra_type size. Volatile applies to every element access. Has no
  // result, the backend can not emit it so IntrinExpand turns what is left
  // into a loop.
  Memcpy,
  // Memmove(dst, src, count): like Memcpy (count elements of extra_type, bytes
  // when it is not set) but the regions may overlap, so the result is as if
  // all of src was read before dst was written. Volatile applies to every
  // element access. Has no result, the backend can not emit it so
  // IntrinLowerCalls turns what is left into a call to memmove.
  Memmove,
  // Memcmp(a, b, count): compares count bytes of a and b like libc memcmp
  // (negative, zero or positive i32, the sign is only specified for the first
  // differing byte compared as unsigned char). Only reads memory. The backend
  // can not emit it, IntrinLowerCalls turns what is left into a call to
  // memcmp.
  Memcmp,
  // MaskedLoad(ptr, mask, passthru): vector load, lane i is read from
  // ptr + i * element size when mask lane i is set and taken from passthru
  // otherwise. Lanes with a clear mask are not accessed, so they may be out of
  // bounds. The mask is a vector of i1 (a vector compare).
  MaskedLoad,
  // MaskedStore(ptr, mask, value): writes the lanes of value whose mask lane
  // is set. Has no result.
  MaskedStore,
  // Gather(base, indices, mask, passthru): lane i is read from
  // base + indices[i] * element size when mask lane i is set and taken from
  // passthru otherwise. The indices are a vector of i32 or i64 (signed) with as
  // many lanes as the result, masked off lanes are not accessed. The form
  // Gather(base, indices) with two operands reads every lane.
  Gather,
  // InlineAsm(id, inputs...): operand 0 is an i32 constant, the id of an
  // AsmInfo of the module table (ContextData::asm_info), the rest are the
  // input values in constraint order. The results are the outputs. Acts like
  // a call to an unknown function: reads and writes memory and has effects.
  InlineAsm,
  // Same operands, but the asm neither touches memory nor has other effects,
  // so it is only a function of its inputs (no memory operands, no memory
  // clobber, not volatile).
  InlineAsmPure,
  // TlsAddress(global): address of the calling thread's copy of a thread
  // local global (llvm.threadlocal.address). Touches no memory, same value for
  // the whole life of a thread.
  TlsAddress,
};

enum class ConversionSubType : u16 {
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

enum class ICmpInstrSubType : u16 {
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

};

enum class FCmpInstrSubType : u16 {
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

enum class UnaryInstrSubType : u16 {
  INVALID = 0,
  FloatNeg,
  IntNeg,
  Not,
  FloatSqrt,
};

enum class BinaryInstrSubType : u16 {
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
  // exact for udiv sdiv lshr ashr, disjoint for or, nneg for zext, samesign
  // for icmp
  u8 Exact : 1 = 0;
  u8 Disjoint : 1 = 0;
  u8 NNeg : 1 = 0;
  u8 SameSign : 1 = 0;

  // keeps only the poison flags both instructions have, used when one
  // instruction stands in for the other
  void intersect_poison_flags(const InstrAttribs &o) {
    NSW &= o.NSW;
    NUW &= o.NUW;
    InBounds &= o.InBounds;
    Exact &= o.Exact;
    Disjoint &= o.Disjoint;
    NNeg &= o.NNeg;
    SameSign &= o.SameSign;
  }
  void clear_poison_flags() {
    NSW = NUW = InBounds = Exact = Disjoint = NNeg = SameSign = 0;
  }
};

class InstrData : public Used, public InstrAttribs {
 public:
  InstrType instr_type;
  u16 subtype;

 private:
  // type of result 0, void when there is no result
  TypeR value_type_;

 public:
  IRVec<ValueR> args;
  // also holds the result types of calls with several results, which are the
  // types of all results (result 0 is also in value_type_ and what gets
  // read), immutable once set and in the IR arena
  BBList bbs;
  BasicBlock parent;
  // where the instruction was last found in `parent->instructions`, only a
  // hint (see BasicBlockData::find_instr), never trust it
  u32 pos_hint = 0;
  // the function may have access metadata for this instruction, a copy of the
  // data has the flag but not the entry
  bool has_access_meta = false;

 private:
  u8 n_results_;

  static u8 default_n_results(InstrType ty, TypeR vty);

 public:
  InstrData(InstrType ty, TypeR vty, BasicBlock parent)
      : instr_type(ty),
        subtype(0),
        value_type_(vty),
        parent(parent),
        n_results_(default_n_results(ty, vty)) {}
  InstrData(InstrType ty, u32 subtype, TypeR vty, BasicBlock parent)
      : instr_type(ty),
        subtype(subtype),
        value_type_(vty),
        parent(parent),
        n_results_(default_n_results(ty, vty)) {}

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
  // type of the only result, void for instructions without a result
  [[nodiscard]] TypeR result_type() const {
    ASSERT(n_results_ <= 1);
    return value_type_;
  }
  // type of result 0, also valid for instructions without a result
  [[nodiscard]] TypeR result_type_0() const { return value_type_; }
  [[nodiscard]] TypeR result_type(u32 idx) const {
    ASSERT(idx < n_results_);
    return idx == 0 ? value_type_ : bbs.types()[idx];
  }
  // empty without results
  [[nodiscard]] std::span<const TypeR> result_types() const {
    if (n_results_ <= 1) {
      return {&value_type_, n_results_};
    }
    return {bbs.types(), n_results_};
  }
  // sets the type of the only result (void for none), drops extra results
  void set_result_type(TypeR ty) {
    value_type_ = ty;
    bbs.clear_types();
    n_results_ = default_n_results(instr_type, ty);
  }
  // sets all result types of an instruction that may have several (calls),
  // a single void entry means no result
  void set_result_types(std::span<const TypeR> tys);
  [[nodiscard]] u32 n_results() const { return n_results_; }
  [[nodiscard]] bool verify(const BasicBlockData *) const;
  [[nodiscard]] bool has_result() const { return n_results() > 0; }
  [[nodiscard]] bool has_one_result() const { return n_results() == 1; }
  [[nodiscard]] bool is_multi_result() const { return n_results() > 1; }
  [[nodiscard]] bool is_critical() const;
  /*true if the instructions arguments can be swapped without changing the
   * result*/
  [[nodiscard]] bool is_commutative() const;
  [[nodiscard]] bool pot_modifies_mem() const;
  // element size in bytes of a memcpy intrinsic, its count counts elements
  [[nodiscard]] u64 memcpy_elem_size() const;
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

  [[nodiscard]] constexpr bool is(ReduceInstrSubType ty) const {
    return instr_type == InstrType::ReduceInstr &&
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

  static InstrData get_extract_element(TypeR ty);
  static InstrData get_insert_element(TypeR ty);
  static InstrData get_smod(TypeR ty);
  static InstrData get_umod(TypeR ty);
  static InstrData get_intrinsic(TypeR ty, IntrinsicSubType sub_type);
  static InstrData get_vector(TypeR ty, VectorISubType sub_type);
  static InstrData get_reduce(TypeR ty, ReduceInstrSubType sub_type);
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

// the generation field of the refs (slot_check_level 2) makes every ref bigger
#if SLOT_CHECK_LEVEL >= 2
static_assert(sizeof(InstrData) == 120, "keep InstrData small");
#else
static_assert(sizeof(InstrData) == 96, "keep InstrData small");
#endif

}  // namespace foptim::fir
