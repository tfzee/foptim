#include "WFVector.hpp"

#include <fmt/base.h>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::optim {

constexpr bool debug_print = false;

std::optional<i64> can_whole_function_vectorize(fir::Function& func,
                                                u64 lanes) {
  // just ignore control flow for now
  // also ignore memory stuff for now
  if (func.basic_blocks.size() > 1 || !func.attribs.mem_read_none || func.attribs.variadic ||
      func.attribs.no_return) {
    return {};
  }
  switch (func.attribs.linkage) {
    case fir::Linkage::Weak:
    case fir::Linkage::WeakODR:
    case fir::Linkage::LinkOnce:
      return {};
    case fir::Linkage::External:
    case fir::Linkage::Internal:
    case fir::Linkage::LinkOnceODR:
      break;
  }
  i64 cost = 0;
  for (auto bb : func.basic_blocks) {
    for (auto instr : bb->instructions) {
      switch (instr->instr_type) {
        case fir::InstrType::CallInstr:
          if (instr->args[0].is_constant_func()) {
            if (!instr->args[0]
                     .as_constant()
                     ->as_func()
                     .func->attribs.maybe_can_wfvec) {
              return {};
            }
          }
          break;
        case fir::InstrType::BinaryInstr:
          switch ((fir::BinaryInstrSubType)instr->subtype) {
            case fir::BinaryInstrSubType::FloatAdd:
            case fir::BinaryInstrSubType::FloatMul:
            case fir::BinaryInstrSubType::IntAdd:
            case fir::BinaryInstrSubType::IntSub:
            case fir::BinaryInstrSubType::IntMul:
              cost -= lanes;
              break;
            case fir::BinaryInstrSubType::FloatSub:
            case fir::BinaryInstrSubType::FloatDiv:
            case fir::BinaryInstrSubType::INVALID:
            case fir::BinaryInstrSubType::IntSRem:
            case fir::BinaryInstrSubType::IntURem:
            case fir::BinaryInstrSubType::IntSDiv:
            case fir::BinaryInstrSubType::IntUDiv:
            case fir::BinaryInstrSubType::Shl:
            case fir::BinaryInstrSubType::Shr:
            case fir::BinaryInstrSubType::AShr:
            case fir::BinaryInstrSubType::And:
            case fir::BinaryInstrSubType::Or:
            case fir::BinaryInstrSubType::Xor:
              if (debug_print) {
                fmt::println("FAILED {}", instr);
              }
              return {};
          }
          break;
        case fir::InstrType::ReturnInstr:
          if (instr->args.size() != 1) {
            return {};
          }
          break;
        case fir::InstrType::Intrinsic:
          switch ((fir::IntrinsicSubType)instr->subtype) {
            case fir::IntrinsicSubType::FAbs:
              cost -= lanes;
              break;
            case fir::IntrinsicSubType::INVALID:
            case fir::IntrinsicSubType::CTLZ:
            case fir::IntrinsicSubType::VA_start:
            case fir::IntrinsicSubType::VA_end:
            case fir::IntrinsicSubType::Abs:
            case fir::IntrinsicSubType::UMin:
            case fir::IntrinsicSubType::UMax:
            case fir::IntrinsicSubType::SMin:
            case fir::IntrinsicSubType::SMax:
            case fir::IntrinsicSubType::FMin:
            case fir::IntrinsicSubType::FMax:
            case fir::IntrinsicSubType::PopCnt:
            case fir::IntrinsicSubType::FRound:
            case fir::IntrinsicSubType::FCeil:
            case fir::IntrinsicSubType::FFloor:
            case fir::IntrinsicSubType::FTrunc:
            case fir::IntrinsicSubType::IsConstant:
              break;
          }
          break;
        case fir::InstrType::ICmp:
        case fir::InstrType::FCmp:
        case fir::InstrType::UnaryInstr:
        case fir::InstrType::ExtractValue:
        case fir::InstrType::InsertValue:
        case fir::InstrType::VectorInstr:
        case fir::InstrType::ITrunc:
        case fir::InstrType::ZExt:
        case fir::InstrType::SExt:
        case fir::InstrType::Conversion:
        case fir::InstrType::SelectInstr:
        case fir::InstrType::BranchInstr:
        case fir::InstrType::CondBranchInstr:
        case fir::InstrType::SwitchInstr:
        case fir::InstrType::Unreachable:
        case fir::InstrType::AllocaInstr:
        case fir::InstrType::LoadInstr:
        case fir::InstrType::StoreInstr:
        case fir::InstrType::AtomicRMW:
        case fir::InstrType::Fence:
          if (debug_print) {
            fmt::println("FAILED {}", instr);
          }
          return {};
      }
    }
  }
  (void)func;
  (void)lanes;
  return {cost};
}

fir::ValueR convert_value(fir::ContextData* ctx, fir::Builder& buh,
                          fir::ValueR v, u64 n_lanes,
                          fir::ContextData::V2VMap& subs) {
  if (v.is_constant()) {
    return buh.build_vbroadcast(v, ctx->get_vec_type(v.get_type(), n_lanes));
  } else {
    return subs.at(v);
  }
}

std::optional<fir::FunctionR> whole_function_vectorize(fir::Function& func,
                                                       u64 n_lanes) {
  auto* ctx = func.ctx;
  IRVec<fir::TypeR> new_args;
  for (auto arg : func.func_ty->as_func().arg_types) {
    new_args.push_back(ctx->get_vec_type(arg, n_lanes));
  }

  auto new_func_type = ctx->get_func_ty(
      ctx->get_vec_type(func.func_ty->as_func().return_type, n_lanes),
      std::move(new_args));
  auto new_name = fmt::format("{}_wfvec_{}", func.getName(), n_lanes);

  auto new_f = ctx->create_function(new_name.c_str(), new_func_type);
  new_f.func->attribs = func.attribs;
  new_f.func->attribs.maybe_can_wfvec = false;
  fir::ContextData::V2VMap subs;
  // for now
  ASSERT(func.n_bbs() == 1);
  {
    auto bb = func.basic_blocks[0];
    auto nbb = new_f->basic_blocks[0];
    auto buh = nbb.builder_at_end();
    for (size_t arg_i = 0; arg_i < bb->n_args(); arg_i++) {
      subs.insert(
          {fir::ValueR{bb->args[arg_i]}, fir::ValueR{nbb->args[arg_i]}});
    }
    for (auto instr : bb->instructions) {
      switch (instr->instr_type) {
        case fir::InstrType::BinaryInstr:
          switch ((fir::BinaryInstrSubType)instr->subtype) {
            case fir::BinaryInstrSubType::IntAdd:
            case fir::BinaryInstrSubType::IntSub:
            case fir::BinaryInstrSubType::IntMul:
            case fir::BinaryInstrSubType::FloatAdd:
            case fir::BinaryInstrSubType::FloatMul: {
              auto new_i = buh.build_binary_op(
                  convert_value(ctx, buh, instr->args[0], n_lanes, subs),
                  convert_value(ctx, buh, instr->args[1], n_lanes, subs),
                  (fir::BinaryInstrSubType)instr->subtype);
              subs.insert({fir::ValueR{instr}, new_i});
              break;
            }
            case fir::BinaryInstrSubType::INVALID:
            case fir::BinaryInstrSubType::IntSRem:
            case fir::BinaryInstrSubType::IntURem:
            case fir::BinaryInstrSubType::IntSDiv:
            case fir::BinaryInstrSubType::IntUDiv:
            case fir::BinaryInstrSubType::Shl:
            case fir::BinaryInstrSubType::Shr:
            case fir::BinaryInstrSubType::AShr:
            case fir::BinaryInstrSubType::And:
            case fir::BinaryInstrSubType::Or:
            case fir::BinaryInstrSubType::Xor:
            case fir::BinaryInstrSubType::FloatSub:
            case fir::BinaryInstrSubType::FloatDiv:
              fmt::println("{}", instr);
              TODO("impl");
          }
          break;
        case fir::InstrType::ReturnInstr: {
          if (instr->args.size() == 1) {
            buh.build_return(
                convert_value(ctx, buh, instr->args[0], n_lanes, subs));
          } else {
            TODO("impl");
          }
        } break;
        case fir::InstrType::Intrinsic:
          switch ((fir::IntrinsicSubType)instr->subtype) {
            case fir::IntrinsicSubType::FAbs: {
              auto new_i = buh.build_intrinsic(
                  convert_value(ctx, buh, instr->args[0], n_lanes, subs),
                  fir::IntrinsicSubType::FAbs);
              subs.insert({fir::ValueR{instr}, new_i});
            } break;
            case fir::IntrinsicSubType::INVALID:
            case fir::IntrinsicSubType::CTLZ:
            case fir::IntrinsicSubType::VA_start:
            case fir::IntrinsicSubType::VA_end:
            case fir::IntrinsicSubType::Abs:
            case fir::IntrinsicSubType::UMin:
            case fir::IntrinsicSubType::UMax:
            case fir::IntrinsicSubType::SMin:
            case fir::IntrinsicSubType::SMax:
            case fir::IntrinsicSubType::FMin:
            case fir::IntrinsicSubType::FMax:
            case fir::IntrinsicSubType::PopCnt:
            case fir::IntrinsicSubType::FRound:
            case fir::IntrinsicSubType::FCeil:
            case fir::IntrinsicSubType::FFloor:
            case fir::IntrinsicSubType::FTrunc:
            case fir::IntrinsicSubType::IsConstant:
              break;
          }
          break;
        case fir::InstrType::ICmp:
        case fir::InstrType::FCmp:
        case fir::InstrType::UnaryInstr:
        case fir::InstrType::ExtractValue:
        case fir::InstrType::InsertValue:
        case fir::InstrType::VectorInstr:
        case fir::InstrType::ITrunc:
        case fir::InstrType::ZExt:
        case fir::InstrType::SExt:
        case fir::InstrType::Conversion:
        case fir::InstrType::SelectInstr:
        case fir::InstrType::CallInstr:
        case fir::InstrType::BranchInstr:
        case fir::InstrType::CondBranchInstr:
        case fir::InstrType::SwitchInstr:
        case fir::InstrType::Unreachable:
        case fir::InstrType::AllocaInstr:
        case fir::InstrType::LoadInstr:
        case fir::InstrType::StoreInstr:
        case fir::InstrType::AtomicRMW:
        case fir::InstrType::Fence:
          fmt::println("{}", instr);
          TODO("impl");
      }
    }
  }
  // fmt::println("{:cd}", *new_f.func);
  return {new_f};
}
}  // namespace foptim::optim
