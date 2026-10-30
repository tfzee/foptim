#pragma once
#include <tuple>
#include <utility>

#include "ir/constant_value.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

// Small combinator library to describe FIR trees for instruction selection.
// A pattern is built from the functions below, a match binds the parts the
// body wants to look at to local variables:(might hcange this)
//   fir::Instr add;
//   if (!pm::matchh(root, pm::Load(pm::capture(add, pm::PtrAdd(pm::Any,
//   pm::Any)))))
//     return false;
// Everything is a template on literal types, so a match is a chain of inlined
// comparisons without allocation or indirect calls.(hopefully as clena as
// possible)
namespace foptim::fmir::pm {

struct AnyM {
  [[nodiscard]] constexpr bool match(fir::ValueR /*unused*/) const {
    return true;
  }
};
inline constexpr AnyM Any{};

// binds any value
struct ValueM {
  fir::ValueR *out;
  [[nodiscard]] bool match(fir::ValueR v) const {
    *out = v;
    return true;
  }
};
inline ValueM value(fir::ValueR &out) { return {&out}; }

// any constant
struct ConstM {
  [[nodiscard]] bool match(fir::ValueR v) const { return v.is_constant(); }
};
inline constexpr ConstM Const{};

// integer constant, optionally bound and restricted by a predicate on the value
template <class Pred>
struct ConstIntM {
  i128 *out;
  Pred pred;
  [[nodiscard]] bool match(fir::ValueR v) const {
    if (!v.is_constant() || !v.as_constant()->is_int()) {
      return false;
    }
    auto val = v.as_constant()->as_int();
    if (!pred(val)) {
      return false;
    }
    if (out != nullptr) {
      *out = val;
    }
    return true;
  }
};
inline ConstIntM<bool (*)(i128)> const_int() {
  return {.out = nullptr, .pred = [](i128) { return true; }};
}
inline ConstIntM<bool (*)(i128)> const_int(i128 &out) {
  return {.out = &out, .pred = [](i128) { return true; }};
}
template <class Pred>
ConstIntM<Pred> const_int(i128 &out, Pred pred) {
  return {&out, pred};
}
template <class Pred>
ConstIntM<Pred> const_int_where(Pred pred) {
  return {nullptr, pred};
}
// exactly this integer
inline auto const_int_is(i128 expected) {
  return const_int_where([expected](i128 v) { return v == expected; });
}

// restrict the type of a value
template <class Pred, class M>
struct OfTypeM {
  Pred pred;
  M inner;
  [[nodiscard]] bool match(fir::ValueR v) const {
    return pred(v.get_type()) && inner.match(v);
  }
};
template <class Pred, class M>
OfTypeM<Pred, M> of_type(Pred pred, M inner) {
  return {pred, inner};
}

// restrict a value with an arbitrary predicate on it
template <class Pred, class M>
struct WhereM {
  Pred pred;
  M inner;
  [[nodiscard]] bool match(fir::ValueR v) const {
    return inner.match(v) && pred(v);
  }
};
template <class Pred, class M>
WhereM<Pred, M> where(Pred pred, M inner) {
  return {pred, inner};
}

template <class A, class B>
struct EitherM {
  A a;
  B b;
  [[nodiscard]] bool match(fir::ValueR v) const {
    return a.match(v) || b.match(v);
  }
};
template <class A, class B>
EitherM<A, B> either(A a, B b) {
  return {a, b};
}

// An instruction of the given kind. Subtype 0 means any subtype. The
// arguments match the operands from the front, operands not mentioned are not
// looked at. All of them have to match it.
template <fir::InstrType Type, u32 Sub, class... Args>
struct InstrM {
  static constexpr fir::InstrType type = Type;
  static constexpr u32 subtype = Sub;

  std::tuple<Args...> args;
  fir::Instr *out = nullptr;

  [[nodiscard]] bool match_instr(fir::Instr instr) const {
    if (instr->get_instr_type() != Type) {
      return false;
    }
    if constexpr (Sub != 0) {
      if (instr->get_instr_subtype() != Sub) {
        return false;
      }
    }
    if (!match_args(instr, std::index_sequence_for<Args...>{})) {
      return false;
    }
    if (out != nullptr) {
      *out = instr;
    }
    return true;
  }

  [[nodiscard]] bool match(fir::ValueR v) const {
    if (!v.is_instr()) {
      return false;
    }
    return match_instr(v.as_instr());
  }

  // binds the matched instruction
  InstrM bind(fir::Instr &o) const {
    auto res = *this;
    res.out = &o;
    return res;
  }

 private:
  template <size_t... I>
  bool match_args(fir::Instr instr,
                  std::index_sequence<I...> /*unused*/) const {
    if constexpr (sizeof...(Args) > 0) {
      ASSERT(instr->args.size() >= sizeof...(Args));
    }
    return (std::get<I>(args).match(instr->args[I]) && ...);
  }
};

// Names the kind of an instruction. Calling it with sub patterns for the
// operands makes the pattern, using it as is only names the kind (what a rule
// registers itself for).
template <fir::InstrType Type, u32 Sub = 0>
struct Op {
  static constexpr fir::InstrType type = Type;
  static constexpr u32 subtype = Sub;

  template <class... Args>
  constexpr InstrM<Type, Sub, Args...> operator()(Args... args) const {
    return {std::tuple<Args...>(args...)};
  }
};

// Operands in either order. Only for two operand instructions.
template <class Inner, class A, class B>
struct CommutativeM {
  Inner base;
  A a;
  B b;
  static constexpr fir::InstrType type = Inner::type;
  static constexpr u32 subtype = Inner::subtype;

  [[nodiscard]] bool match_instr(fir::Instr instr) const {
    if (instr->get_instr_type() != Inner::type) {
      return false;
    }
    if constexpr (Inner::subtype != 0) {
      if (instr->get_instr_subtype() != Inner::subtype) {
        return false;
      }
    }
    ASSERT(instr->args.size() >= 2);
    return (a.match(instr->args[0]) && b.match(instr->args[1])) ||
           (a.match(instr->args[1]) && b.match(instr->args[0]));
  }
  [[nodiscard]] bool match(fir::ValueR v) const {
    return v.is_instr() && match_instr(v.as_instr());
  }
};
template <fir::InstrType Type, u32 Sub, class A, class B>
CommutativeM<Op<Type, Sub>, A, B> commutative(Op<Type, Sub> op, A a, B b) {
  return {op, a, b};
}

// binds the instruction a pattern matched
template <class M>
struct CaptureM {
  fir::Instr *out;
  M inner;
  [[nodiscard]] bool match(fir::ValueR v) const {
    if (!v.is_instr() || !inner.match(v)) {
      return false;
    }
    *out = v.as_instr();
    return true;
  }
};
template <class M>
CaptureM<M> capture(fir::Instr &out, M inner) {
  return {&out, inner};
}

// matches the instruction itself (the root of a rule)
template <class M>
bool match(fir::Instr instr, const M &pattern) {
  return pattern.match_instr(instr);
}
template <class M>
bool match(fir::ValueR val, const M &pattern) {
  return pattern.match(val);
}

using fir::InstrType;
using Bin = fir::BinaryInstrSubType;
using Un = fir::UnaryInstrSubType;
using Vec = fir::VectorISubType;
using Intrin = fir::IntrinsicSubType;
using Red = fir::ReduceInstrSubType;

template <fir::BinaryInstrSubType S>
using BinOp = Op<InstrType::BinaryInstr, static_cast<u32>(S)>;
template <fir::UnaryInstrSubType S>
using UnOp = Op<InstrType::UnaryInstr, static_cast<u32>(S)>;
template <fir::VectorISubType S>
using VecOp = Op<InstrType::VectorInstr, static_cast<u32>(S)>;
template <fir::ReduceInstrSubType S>
using ReduceOp = Op<InstrType::ReduceInstr, static_cast<u32>(S)>;
template <fir::IntrinsicSubType S>
using IntrinOp = Op<InstrType::Intrinsic, static_cast<u32>(S)>;

inline constexpr BinOp<Bin::IntAdd> IntAdd;
inline constexpr BinOp<Bin::PtrAdd> PtrAdd;
inline constexpr BinOp<Bin::IntSub> IntSub;
inline constexpr BinOp<Bin::IntMul> IntMul;
inline constexpr BinOp<Bin::IntSRem> SRem;
inline constexpr BinOp<Bin::IntURem> URem;
inline constexpr BinOp<Bin::IntSDiv> SDiv;
inline constexpr BinOp<Bin::IntUDiv> UDiv;
inline constexpr BinOp<Bin::And> And;
inline constexpr BinOp<Bin::Or> Or;
inline constexpr BinOp<Bin::Xor> Xor;
inline constexpr BinOp<Bin::FloatAdd> FloatAdd;
inline constexpr BinOp<Bin::FloatSub> FloatSub;
inline constexpr BinOp<Bin::FloatMul> FloatMul;
inline constexpr BinOp<Bin::FloatDiv> FloatDiv;
inline constexpr BinOp<Bin::Shl> Shl;
inline constexpr BinOp<Bin::Shr> Shr;
inline constexpr BinOp<Bin::AShr> AShr;

inline constexpr UnOp<Un::Not> Not;
inline constexpr UnOp<Un::IntNeg> IntNeg;
inline constexpr UnOp<Un::FloatNeg> FloatNeg;
inline constexpr UnOp<Un::FloatSqrt> FloatSqrt;

inline constexpr VecOp<Vec::Broadcast> Broadcast;
inline constexpr VecOp<Vec::Concat> Concat;
inline constexpr VecOp<Vec::Shuffle> Shuffle;
inline constexpr VecOp<Vec::ByteShuffle> ByteShuffle;
inline constexpr VecOp<Vec::ExtractHigh> ExtractHigh;
inline constexpr VecOp<Vec::ExtractLow> ExtractLow;
inline constexpr ReduceOp<Red::Add> ReduceAdd;
inline constexpr ReduceOp<Red::Mul> ReduceMul;
inline constexpr ReduceOp<Red::And> ReduceAnd;
inline constexpr ReduceOp<Red::Or> ReduceOr;
inline constexpr ReduceOp<Red::Xor> ReduceXor;
inline constexpr ReduceOp<Red::SMin> ReduceSMin;
inline constexpr ReduceOp<Red::SMax> ReduceSMax;
inline constexpr ReduceOp<Red::UMin> ReduceUMin;
inline constexpr ReduceOp<Red::UMax> ReduceUMax;
inline constexpr ReduceOp<Red::FMin> ReduceFMin;
inline constexpr ReduceOp<Red::FMax> ReduceFMax;

inline constexpr IntrinOp<Intrin::CTLZ> Ctlz;
inline constexpr IntrinOp<Intrin::CTTZ> Cttz;
inline constexpr IntrinOp<Intrin::PopCnt> PopCnt;
inline constexpr IntrinOp<Intrin::Bswap> Bswap;
inline constexpr IntrinOp<Intrin::VA_end> VaEnd;
inline constexpr IntrinOp<Intrin::VA_start> VaStart;
inline constexpr IntrinOp<Intrin::Abs> Abs;
inline constexpr IntrinOp<Intrin::FAbs> FAbs;
inline constexpr IntrinOp<Intrin::SMin> SMin;
inline constexpr IntrinOp<Intrin::UMin> UMin;
inline constexpr IntrinOp<Intrin::UMax> UMax;
inline constexpr IntrinOp<Intrin::SMax> SMax;
inline constexpr IntrinOp<Intrin::FMax> FMax;
inline constexpr IntrinOp<Intrin::FMin> FMin;
inline constexpr IntrinOp<Intrin::FRound> FRound;
inline constexpr IntrinOp<Intrin::FCeil> FCeil;
inline constexpr IntrinOp<Intrin::FFloor> FFloor;
inline constexpr IntrinOp<Intrin::FTrunc> FTrunc;
inline constexpr IntrinOp<Intrin::Fma> Fma;
inline constexpr IntrinOp<Intrin::Clmul> Clmul;
inline constexpr IntrinOp<Intrin::PackSS> PackSS;
inline constexpr IntrinOp<Intrin::PackUS> PackUS;
inline constexpr IntrinOp<Intrin::PMulHW> PMulHW;
inline constexpr IntrinOp<Intrin::PMaddWD> PMaddWD;
inline constexpr IntrinOp<Intrin::PMaddUBSW> PMaddUBSW;
inline constexpr IntrinOp<Intrin::PermD> PermD;
inline constexpr IntrinOp<Intrin::MoveMask> MoveMask;
inline constexpr IntrinOp<Intrin::IsConstant> IsConstant;
inline constexpr IntrinOp<Intrin::MaskedLoad> MaskedLoad;
inline constexpr IntrinOp<Intrin::MaskedStore> MaskedStore;
inline constexpr IntrinOp<Intrin::Gather> Gather;
inline constexpr IntrinOp<Intrin::InlineAsm> InlineAsm;
inline constexpr IntrinOp<Intrin::InlineAsmPure> InlineAsmPure;
inline constexpr IntrinOp<Intrin::TlsAddress> TlsAddress;

inline constexpr Op<InstrType::ICmp> ICmp;
inline constexpr Op<InstrType::FCmp> FCmp;
inline constexpr Op<InstrType::SelectInstr> Select;
inline constexpr Op<InstrType::Conversion> Conversion;
inline constexpr Op<InstrType::SExt> SExt;
inline constexpr Op<InstrType::ZExt> ZExt;
inline constexpr Op<InstrType::ITrunc> ITrunc;
inline constexpr Op<InstrType::ExtractElement> ExtractElement;
inline constexpr Op<InstrType::InsertElement> InsertElement;
inline constexpr Op<InstrType::CallInstr> Call;
inline constexpr Op<InstrType::ReturnInstr> Return;
inline constexpr Op<InstrType::BranchInstr> Branch;
inline constexpr Op<InstrType::CondBranchInstr> CondBranch;
inline constexpr Op<InstrType::SwitchInstr> Switch;
inline constexpr Op<InstrType::Unreachable> Unreachable;
inline constexpr Op<InstrType::AllocaInstr> Alloca;
inline constexpr Op<InstrType::LoadInstr> Load;
inline constexpr Op<InstrType::StoreInstr> Store;
inline constexpr Op<InstrType::AtomicRMW> AtomicRMW;
inline constexpr Op<InstrType::Fence> Fence;

}  // namespace foptim::fmir::pm
