#include "optim/cost_model/fir_lowering_estimate.hpp"

#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"

namespace foptim::optim::cost_model {
namespace {
bool is_fusable_icmp_pred(fir::ICmpInstrSubType pred) {
  using fir::ICmpInstrSubType;
  switch (pred) {
    case ICmpInstrSubType::SLT:
    case ICmpInstrSubType::SGT:
    case ICmpInstrSubType::SLE:
    case ICmpInstrSubType::SGE:
    case ICmpInstrSubType::ULT:
    case ICmpInstrSubType::ULE:
    case ICmpInstrSubType::UGT:
    case ICmpInstrSubType::UGE:
    case ICmpInstrSubType::EQ:
    case ICmpInstrSubType::NE:
      return true;
    default:
      return false;
  }
}

bool is_address_math(fir::Instr instr) {
  return instr->is(fir::BinaryInstrSubType::PtrAdd) ||
         instr->is(fir::BinaryInstrSubType::Shl);
}

u32 vec_bitwidth(fir::TypeR ty) { return ty->get_bitwidth(); }

}  // namespace

void detect_fold_context(fir::Instr instr, CostCtx &ctx) {
  ctx.folds_into_address = false;
  ctx.folds_into_user = false;
  if (instr->n_results() != 1 || instr->get_uses().size() != 1) {
    return;
  }
  const fir::Use &use = instr->get_uses()[0];
  if (use.type != fir::UseType::NormalArg) {
    return;
  }
  fir::Instr user = use.user;

  // add/shl feeding a pointer operand folds into the addressing mode
  if (is_address_math(instr) && use.argId == 0 &&
      (user->is(fir::InstrType::LoadInstr) ||
       user->is(fir::InstrType::StoreInstr))) {
    ctx.folds_into_address = true;
    return;
  }

  // load whose only use is an ALU or compare op folds into it
  if (instr->is(fir::InstrType::LoadInstr) &&
      (user->is(fir::InstrType::BinaryInstr) ||
       user->is(fir::InstrType::UnaryInstr) ||
       user->is(fir::InstrType::ICmp) || user->is(fir::InstrType::FCmp))) {
    ctx.folds_into_user = true;
    return;
  }

  // compare whose only use is a fusable branch macro fuses into one cmp+jcc uop
  if (instr->is(fir::InstrType::ICmp) &&
      user->is(fir::InstrType::CondBranchInstr) &&
      is_fusable_icmp_pred(
          static_cast<fir::ICmpInstrSubType>(instr->get_instr_subtype()))) {
    ctx.folds_into_user = true;
    return;
  }
  if (instr->is(fir::InstrType::FCmp) &&
      user->is(fir::InstrType::CondBranchInstr)) {
    ctx.folds_into_user = true;
    return;
  }
}

void estimate_lowering(fir::Instr instr, const CostCtx &ctx, LoweredOps &out) {
  switch (instr->get_instr_type()) {
    case fir::InstrType::BinaryInstr: {
      const auto sub =
          static_cast<fir::BinaryInstrSubType>(instr->get_instr_subtype());
      if (is_address_math(instr) && ctx.folds_into_address) {
        return;  // free: absorbed into the user's memory operand
      }
      switch (sub) {
        case fir::BinaryInstrSubType::IntMul:
          out.push_back({X86OpClass::IMUL_RR, 1.0});
          return;
        case fir::BinaryInstrSubType::IntSDiv:
        case fir::BinaryInstrSubType::IntSRem:
          out.push_back({instr->result_type()->get_bitwidth() > 32
                             ? X86OpClass::IDIV64
                             : X86OpClass::IDIV32,
                         1.0});
          return;
        case fir::BinaryInstrSubType::IntUDiv:
        case fir::BinaryInstrSubType::IntURem:
          out.push_back({instr->result_type()->get_bitwidth() > 32
                             ? X86OpClass::IDIV64
                             : X86OpClass::IDIV32,
                         1.0});
          return;
        case fir::BinaryInstrSubType::Shl:
        case fir::BinaryInstrSubType::Shr:
        case fir::BinaryInstrSubType::AShr:
          out.push_back({ctx.operand_is_const[1] ? X86OpClass::SHIFT_RI
                                                  : X86OpClass::SHIFT_RR,
                         1.0});
          return;
        case fir::BinaryInstrSubType::FloatAdd:
        case fir::BinaryInstrSubType::FloatSub:
        case fir::BinaryInstrSubType::FloatMul:
          out.push_back({X86OpClass::FP_ALU, 1.0});
          return;
        case fir::BinaryInstrSubType::FloatDiv:
          out.push_back({X86OpClass::FP_DIV, 1.0});
          return;
        default:
          out.push_back({ctx.operand_is_const[1] ? X86OpClass::ALU_RI
                                                  : X86OpClass::ALU_RR,
                         1.0});
          return;
      }
    }
    case fir::InstrType::UnaryInstr: {
      const auto sub =
          static_cast<fir::UnaryInstrSubType>(instr->get_instr_subtype());
      if (sub == fir::UnaryInstrSubType::FloatSqrt) {
        out.push_back({X86OpClass::FP_DIV, 1.0});  // sqrt ~ div-class latency
        return;
      }
      if (sub == fir::UnaryInstrSubType::FloatNeg) {
        out.push_back({X86OpClass::FP_ALU, 1.0});
        return;
      }
      out.push_back({X86OpClass::ALU_RR, 1.0});  // Not, IntNeg
      return;
    }
    case fir::InstrType::ICmp: {
      // with folds_into_user cost() zeroes all but Latency, the value still
      // takes time
      out.push_back({X86OpClass::SETCC, 1.0});
      return;
    }
    case fir::InstrType::FCmp: {
      out.push_back({X86OpClass::FP_CMP, 1.0});
      return;
    }
    case fir::InstrType::SelectInstr:
      out.push_back({X86OpClass::CMOV, 1.0});
      return;
    case fir::InstrType::LoadInstr:
      out.push_back({X86OpClass::LOAD, 1.0});
      return;
    case fir::InstrType::StoreInstr:
      out.push_back({X86OpClass::STORE, 1.0});
      return;
    case fir::InstrType::AllocaInstr:
      return;  // resolved at compile time into the frame layout, no uop
    case fir::InstrType::ZExt: {
      // free for the implicit zero extension of a 32 bit write, else a movzx
      const u32 src_bits = instr->args[0].get_type()->get_bitwidth();
      const u32 dst_bits = instr->result_type()->get_bitwidth();
      if (src_bits == 32 && dst_bits == 64) {
        return;
      }
      out.push_back({X86OpClass::MOV_RR, 1.0});
      return;
    }
    case fir::InstrType::ITrunc:
      return;  // free: just read a narrower sub-register
    case fir::InstrType::SExt:
      out.push_back({X86OpClass::MOV_RR, 1.0});  // movsx
      return;
    case fir::InstrType::Conversion: {
      const auto sub =
          static_cast<fir::ConversionSubType>(instr->get_instr_subtype());
      switch (sub) {
        case fir::ConversionSubType::BitCast:
        case fir::ConversionSubType::PtrToInt:
        case fir::ConversionSubType::IntToPtr:
          return;  // free reinterpretation
        default:
          out.push_back({X86OpClass::FP_ALU, 1.0});  // the float<->int family
          return;
      }
    }
    case fir::InstrType::CallInstr:
      out.push_back({X86OpClass::CALL, 1.0});
      return;
    case fir::InstrType::BranchInstr:
      out.push_back({X86OpClass::JMP, 1.0});
      return;
    case fir::InstrType::CondBranchInstr: {
      const fir::ValueR cond = instr->args[0];
      if (cond.is_instr(fir::InstrType::ICmp)) {
        const fir::Instr cmp = cond.as_instr();
        const auto pred =
            static_cast<fir::ICmpInstrSubType>(cmp->get_instr_subtype());
        if (cmp->get_uses().size() == 1 && is_fusable_icmp_pred(pred)) {
          out.push_back({X86OpClass::CMP_JCC_FUSED, 1.0});
          return;
        }
      }
      if (cond.is_instr(fir::InstrType::FCmp)) {
        const fir::Instr cmp = cond.as_instr();
        if (cmp->get_uses().size() == 1) {
          out.push_back({X86OpClass::CMP_JCC_FUSED, 1.0});
          return;
        }
      }
      out.push_back({X86OpClass::JCC, 1.0});
      return;
    }
    case fir::InstrType::SwitchInstr:
      out.push_back({X86OpClass::JMP, 1.0});
      return;
    case fir::InstrType::ReturnInstr:
    case fir::InstrType::Unreachable:
      return;  // part of the epilogue / no runtime cost
    case fir::InstrType::AtomicRMW:
      // approximated as a locked load+store pair, no ordering or contention
      out.push_back({X86OpClass::LOAD, 1.0});
      out.push_back({X86OpClass::STORE, 1.0});
      return;
    case fir::InstrType::Fence:
      return;
    case fir::InstrType::Intrinsic: {
      const auto sub =
          static_cast<fir::IntrinsicSubType>(instr->get_instr_subtype());
      switch (sub) {
        case fir::IntrinsicSubType::PopCnt:
          out.push_back({X86OpClass::POPCNT, 1.0});
          return;
        case fir::IntrinsicSubType::CTLZ:
          out.push_back({X86OpClass::LZCNT, 1.0});
          return;
        case fir::IntrinsicSubType::CTTZ:
          out.push_back({X86OpClass::TZCNT, 1.0});
          return;
        case fir::IntrinsicSubType::Bswap:
          out.push_back({X86OpClass::BSWAP, 1.0});
          return;
        case fir::IntrinsicSubType::FMin:
        case fir::IntrinsicSubType::FMax:
        case fir::IntrinsicSubType::FMinimum:
        case fir::IntrinsicSubType::FMaximum:
          out.push_back({X86OpClass::FP_CMP, 1.0});
          return;
        case fir::IntrinsicSubType::Fma:
          out.push_back({X86OpClass::FP_ALU, 1.0});
          return;
        default:
          // SMin/SMax/UMin/UMax/Abs and the rest, flat ALU cost
          out.push_back({X86OpClass::ALU_RR, 1.0});
          return;
      }
    }
    case fir::InstrType::VectorInstr: {
      const auto sub =
          static_cast<fir::VectorISubType>(instr->get_instr_subtype());
      if (sub == fir::VectorISubType::Shuffle ||
          sub == fir::VectorISubType::ByteShuffle) {
        out.push_back({X86OpClass::VEC_SHUFFLE, 1.0});
        return;
      }
      const u32 bits = vec_bitwidth(instr->result_type_0());
      out.push_back({bits > 128 ? X86OpClass::VEC_ALU_256
                                 : X86OpClass::VEC_ALU_128,
                     1.0});
      return;
    }
    case fir::InstrType::ReduceInstr:
    case fir::InstrType::ExtractElement:
    case fir::InstrType::InsertElement: {
      const u32 bits = vec_bitwidth(instr->result_type_0());
      out.push_back({bits > 128 ? X86OpClass::VEC_ALU_256
                                 : X86OpClass::VEC_ALU_128,
                     1.0});
      return;
    }
  }
  // fall back instead of UNREACH so a new enumerator is just not free
  out.push_back({X86OpClass::ALU_RR, 1.0});
}

}  // namespace foptim::optim::cost_model
