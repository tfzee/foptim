#pragma once
#include "ir/attributable.hpp"
#include "ir/basic_block_ref.hpp"
#include "use.hpp"
#include "utils/logging.hpp"
#include "utils/todo.hpp"
#include "utils/vec.hpp"
#include "value.hpp"

namespace foptim::fir {
struct ContextData;

enum class InstrType : u8 {
  ICmp,
  BinaryInstr,
  AllocaInstr,

  SExt,
  ReturnInstr,
  DirectCallInstr,

  BranchInstr,
  CondBranchInstr,

  LoadInstr,
  StoreInstr,
};

struct BBRefWithArgs {
  BasicBlock bb;
  FVec<ValueR> args;

  constexpr bool operator==(const BBRefWithArgs &other) const {
    if (bb != other.bb || args.size() != other.args.size()) {
      return false;
    }
    for (size_t i = 0; i < args.size(); i++) {
      if (args[i].eql(other.args[i])) {
        return false;
      }
    }
    return true;
  }
};

enum class ICmpInstrSubType : u32 {
  INVALID = 0,
  SLT,
  ULT,
};

enum class BinaryInstrSubType : u32 {
  INVALID = 0,
  IntAdd,
  // IntSub,
  IntMul,
  // IntDiv,
  // IntMod,

  // IntAnd,
  // IntOr,

  // FloatAdd,
  // FloatSub,
  // FloatMul,
  // FloatDiv,
};

class InstrData : public Used, public Attributable {
  InstrType instr_type;
  u32 subtype;
  TypeR value_type;
  InstrData(InstrType ty, TypeR vty, BasicBlock parent)
      : instr_type(ty), subtype(0), value_type(vty), parent(parent) {}
  InstrData(InstrType ty, u32 subtype, TypeR vty, BasicBlock parent)
      : instr_type(ty), subtype(subtype), value_type(vty), parent(parent) {}

public:
  using Used::add_usage;
  FVec<ValueR> args;
  FVec<BBRefWithArgs> bbs;
  BasicBlock parent;

  constexpr InstrType get_instr_type() const { return instr_type; }
  constexpr u32 get_instr_subtype() const { return subtype; }
  constexpr void set_parent(BasicBlock new_parent) { parent = new_parent; }
  constexpr BasicBlock get_parent() { return parent; }
  constexpr const BasicBlock get_parent() const { return parent; }



  bool verify(const BasicBlockData *, utils::Printer) const;
  bool has_result() const;
  bool is_critical() const;
  bool pot_modifies_mem() const;

  bool has_pot_sideeffects() const;

  constexpr const char *get_name() const {
    switch (instr_type) {
    case InstrType::BinaryInstr:
      switch ((BinaryInstrSubType)subtype) {
      case BinaryInstrSubType::INVALID:
        return "BINARYOP_INVALID";
      case BinaryInstrSubType::IntAdd:
        return "IntAdd";
      case BinaryInstrSubType::IntMul:
        return "IntMul";
      }
    case InstrType::SExt:
      return "SExt";
    case InstrType::CondBranchInstr:
      return "CondBranch";
    case InstrType::DirectCallInstr:
      return "DirectCall";
    case InstrType::LoadInstr:
      return "Load";
    case InstrType::StoreInstr:
      return "Store";
    case InstrType::AllocaInstr:
      return "Alloca";
    case InstrType::ReturnInstr:
      return "Return";
    case InstrType::BranchInstr:
      return "Branch";
    case InstrType::ICmp:
      switch ((ICmpInstrSubType)subtype) {
      case ICmpInstrSubType::INVALID:
        return "INVALID";
      case ICmpInstrSubType::ULT:
        return "IntULT";
      case ICmpInstrSubType::SLT:
        return "IntSLT";
      }
    }
    ASSERT_M(false, "Add your instruction to the get_name function");
    return "UNKNOWN";
  }

  constexpr bool is(InstrType ty) const { return ty == instr_type; }

  constexpr void verify() const {
    switch (instr_type) {
    case InstrType::BinaryInstr:
      ASSERT(args.size() == 2);
      break;
    case InstrType::ICmp:
      ASSERT(args.size() == 2);
      break;
    case InstrType::DirectCallInstr:
      ASSERT(has_attrib("callee"));
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

  static InstrData get_add(TypeR ty);
  static InstrData get_mul(TypeR ty);
  static InstrData get_sext(TypeR ty);
  static InstrData get_int_cmp(TypeR ty, ICmpInstrSubType cmp_ty);
  static InstrData get_return(TypeR ty);
  static InstrData get_direct_call(TypeR ty);
  static InstrData get_alloca(TypeR ty);
  // static InstrData get_gep(TypeR ty);
  static InstrData get_load(TypeR ty);
  static InstrData get_store(TypeR ty);
  static InstrData get_branch(ContextData *ctx);
  static InstrData get_cond_branch(ContextData *ctx);

  bool eql_expr(const InstrData &other) const {
    // TODO: compare on type?? value_type.eql(other.value_type)
    if (instr_type != other.instr_type) {
      return false;
    }
    if (subtype != other.subtype) {
      return false;
    }
    // TODO: attributes
    if (args.size() != other.args.size() || bbs.size() != other.bbs.size()) {
      return false;
    }
    for (size_t i = 0; i < args.size(); i++) {
      if (!args[i].eql(other.args[i])) {
        return false;
      }
    }
    for (size_t i = 0; i < bbs.size(); i++) {
      if (bbs[i] != other.bbs[i]) {
        return false;
      }
    }
    return true;
  }

  constexpr const FVec<Use> &get_uses() const { return uses; }
  constexpr const FVec<ValueR> &get_args() const { return args; }
  constexpr const ValueR &get_arg(size_t indx) const { return args.at(indx); }
  constexpr const FVec<BBRefWithArgs> &get_bb_args() const { return bbs; }
  constexpr TypeR get_type() const { return value_type; }
};

} // namespace foptim::fir
