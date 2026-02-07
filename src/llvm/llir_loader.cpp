#include "llir_loader.hpp"

#include <fmt/base.h>
#include <fmt/color.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>

#include <cstdlib>
#include <deque>
#include <limits>
#include <memory>

#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/context.hpp"
#include "ir/function.hpp"
#include "ir/function_ref.hpp"
#include "ir/global.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/types_ref.hpp"
#include "ir/value.hpp"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "utils/arena.hpp"
#include "utils/job_system.hpp"
#include "utils/parameters.hpp"
#include "utils/set.hpp"

namespace {
using foptim::u32;
using foptim::u64;

using V2VMap = foptim::TMap<const llvm::Value *, foptim::fir::ValueR>;
using B2BMap = foptim::TMap<llvm::BasicBlock *, foptim::fir::BasicBlock>;

inline void convert(llvm::Instruction *any_instr, foptim::fir::Context &fctx,
                    foptim::fir::FunctionR ffunc, foptim::fir::Builder &builder,
                    V2VMap &valueToValue, llvm::Module &mod, B2BMap &b2b);
foptim::fir::TypeR convert_type(llvm::Type *any_ty, foptim::fir::Context &ctx,
                                llvm::Module &module);

inline foptim::fir::ValueR convert_instr_arg(const llvm::Value *value,
                                             foptim::fir::Context &fctx,
                                             foptim::fir::FunctionR ffunc,
                                             foptim::fir::Builder &builder,
                                             V2VMap &valueToValue,
                                             llvm::Module &mod, B2BMap &b2b) {
  (void)ffunc;
  (void)builder;
  if (const auto *const_value = dyn_cast_or_null<llvm::ConstantExpr>(value)) {
    auto *instr = const_value->getAsInstruction();
    ASSERT(instr != nullptr);
    convert(instr, fctx, ffunc, builder, valueToValue, mod, b2b);
    return valueToValue.at(instr);
  }
  if (valueToValue.contains(value)) {
    return valueToValue.at(value);
  }

  if (const auto *int_constant =
          llvm::dyn_cast_or_null<llvm::ConstantInt>(value)) {
    u32 bitwidth = int_constant->getBitWidth();
    auto value = int_constant->getValue();

    if (value.isNegative() && bitwidth != 1) {
      return foptim::fir::ValueR(
          fctx->get_constant_int(value.getSExtValue(), bitwidth));
    }
    return foptim::fir::ValueR(
        fctx->get_constant_int(value.getZExtValue(), bitwidth));
  }
  if (nullptr != llvm::dyn_cast_or_null<llvm::ConstantPointerNull>(value)) {
    return foptim::fir::ValueR(fctx->get_constant_null());
  }
  if (const auto *float_constant =
          llvm::dyn_cast_or_null<llvm::ConstantFP>(value)) {
    auto value = float_constant->getValue();
    auto is_float = float_constant->getType()->isFloatTy();
    auto is_double = float_constant->getType()->isDoubleTy();

    if (is_float) {
      return foptim::fir::ValueR(fctx->get_constant_value(
          value.convertToFloat(), fctx->get_float_type(32)));
    }
    if (is_double) {
      return foptim::fir::ValueR(fctx->get_constant_value(
          value.convertToDouble(), fctx->get_float_type(64)));
    }
  }
  if (nullptr != llvm::dyn_cast_or_null<llvm::Function>(value)) {
    return foptim::fir::ValueR(fctx->get_constant_value(
        fctx->get_function(value->getName().str().c_str())));
  }
  if (const auto *vec_const =
          llvm::dyn_cast_or_null<llvm::ConstantDataVector>(value)) {
    auto num_elem = vec_const->getNumElements();
    auto *elemty = vec_const->getElementType();
    if (elemty->isFloatTy()) {
      foptim::IRVec<foptim::fir::ConstantValueR> vals;
      for (size_t i = 0; i < num_elem; i++) {
        vals.push_back(fctx->get_constant_value(vec_const->getElementAsFloat(i),
                                                fctx->get_float_type(32)));
      }
      return foptim::fir::ValueR(fctx->get_constant_value(
          std::move(vals),
          fctx->get_vec_type(foptim::fir::VectorType::SubType::Floating, 32,
                             num_elem)));
    }
    if (elemty->isDoubleTy()) {
      foptim::IRVec<foptim::fir::ConstantValueR> vals;
      for (size_t i = 0; i < num_elem; i++) {
        vals.push_back(fctx->get_constant_value(
            vec_const->getElementAsDouble(i), fctx->get_float_type(64)));
      }
      return foptim::fir::ValueR(fctx->get_constant_value(
          std::move(vals),
          fctx->get_vec_type(foptim::fir::VectorType::SubType::Floating, 64,
                             num_elem)));
    }
    if (elemty->isIntegerTy()) {
      auto width = elemty->getIntegerBitWidth();
      foptim::IRVec<foptim::fir::ConstantValueR> vals;
      for (size_t i = 0; i < num_elem; i++) {
        vals.push_back(fctx->get_constant_value(
            vec_const->getElementAsAPInt(i).getSExtValue(),
            fctx->get_int_type(width)));
      }
      return foptim::fir::ValueR(fctx->get_constant_value(
          std::move(vals),
          fctx->get_vec_type(foptim::fir::VectorType::SubType::Integer, width,
                             num_elem)));
    }
    llvm::errs() << *vec_const << "\n";
    TODO("impl");
  }
  if (const auto *stru_const =
          llvm::dyn_cast_or_null<llvm::ConstantStruct>(value)) {
    // stru_const->dump();
    auto res_ty = convert_type(stru_const->getType(), fctx, mod);
    auto res_ty_stru = res_ty->as_struct();
    auto ress = foptim::fir::ValueR{fctx->get_poisson_value(res_ty)};
    foptim::fir::ValueR index_v[1];
    for (size_t arg_id = 0; arg_id < res_ty_stru.elems.size(); arg_id++) {
      auto *v = stru_const->getOperand(arg_id);
      auto v_res =
          convert_instr_arg(v, fctx, ffunc, builder, valueToValue, mod, b2b);
      index_v[0] = foptim::fir::ValueR{fctx->get_constant_int(arg_id, 32)};
      ress = builder.build_insert_value(ress, v_res, index_v, res_ty);
    }
    return ress;
  }
  if (const auto *constant =
          llvm::dyn_cast_or_null<llvm::ConstantAggregateZero>(value)) {
    llvm::errs() << constant << " " << typeid(constant).name() << "\n";
    llvm::errs() << *constant << "\n";
    (void)constant;
    TODO("impl const aggr zero");
  }
  if (const auto *undef_constant =
          llvm::dyn_cast_or_null<llvm::UndefValue>(value)) {
    if (undef_constant->getType()->isFloatTy()) {
      return foptim::fir::ValueR(
          fctx->get_poisson_value(fctx->get_float_type(32)));
    }
    if (undef_constant->getType()->isDoubleTy()) {
      return foptim::fir::ValueR(
          fctx->get_poisson_value(fctx->get_float_type(64)));
    }
    if (auto *v =
            dyn_cast_or_null<llvm::IntegerType>(undef_constant->getType())) {
      return foptim::fir::ValueR(
          fctx->get_poisson_value(fctx->get_int_type(v->getBitWidth())));
    }
  }

  llvm::errs() << value << " " << typeid(value).name() << "\n";
  llvm::errs() << *value << "\n";
  TODO("IDK HOW TO HANDLE THIS ARG");
}

foptim::fir::TypeR convert_type(llvm::Type *any_ty, foptim::fir::Context &ctx,
                                llvm::Module &module) {
  if (auto *v = llvm::dyn_cast_or_null<llvm::IntegerType>(any_ty)) {
    u32 width = v->getBitWidth();
    ASSERT_M(width <= 64, "only support 64 bit max width for integers");
    return ctx->get_int_type(width);
  }
  if (any_ty->isFloatTy()) {
    return ctx->get_float_type(32);
  }
  if (any_ty->isDoubleTy()) {
    return ctx->get_float_type(64);
  }
  if (any_ty->isX86_FP80Ty()) {
    fmt::print(fg(fmt::color::red),
               "[WARNING] Unsupported x86_fp80 ty will still try to run with "
               "f64 type instead\n");
    return ctx->get_float_type(64);
  }
  if (llvm::dyn_cast_or_null<llvm::PointerType>(any_ty) != nullptr) {
    return ctx->get_ptr_type();
  }
  if (any_ty->isVoidTy()) {
    return ctx->get_void_type();
  }
  if (auto *stru = llvm::dyn_cast_or_null<llvm::StructType>(any_ty)) {
    foptim::IRVec<foptim::fir::StructType::StructElem> elems(
        stru->getStructNumElements());
    const auto *struct_layout = module.getDataLayout().getStructLayout(stru);
    for (u32 member_id = 0; member_id < stru->getStructNumElements();
         member_id++) {
      auto offset = struct_layout->getElementOffset(member_id);
      auto ty =
          convert_type(stru->getStructElementType(member_id), ctx, module);
      elems[member_id] = {offset, ty};
    }
    return ctx->get_struct_type(std::move(elems));
  }
  if (auto *v = llvm::dyn_cast_or_null<llvm::FunctionType>(any_ty)) {
    auto ret_type = convert_type(v->getReturnType(), ctx, module);

    foptim::IRVec<foptim::fir::TypeR> args;
    args.reserve(v->getNumParams());
    for (size_t i = 0; i < v->getNumParams(); i++) {
      auto *param = v->getParamType(i);
      args.push_back(convert_type(param, ctx, module));
    }
    return ctx->get_func_ty(ret_type, args);
  }
  if (auto *v = llvm::dyn_cast_or_null<llvm::VectorType>(any_ty)) {
    auto sub_type = convert_type(v->getElementType(), ctx, module);
    auto maybe_num = v->getElementCount();
    ASSERT(maybe_num.isFixed());
    auto num = maybe_num.getFixedValue();

    ASSERT(sub_type->is_float() || sub_type->is_int() || sub_type->is_ptr());
    return ctx->get_vec_type(sub_type, num);
  }

  llvm::errs() << "FAILED TO CONVERT LLVM IR TYPE TO NORMAL TYPE TODO\n";
  llvm::errs() << *any_ty << "\n" << "TODO\n";
  std::abort();
}

void convert_alloca(const llvm::Instruction *any_instr,
                    const llvm::AllocaInst *alloca_instr,
                    foptim::fir::Context &fctx, foptim::fir::Builder &builder,
                    V2VMap &valueToValue, llvm::Module &mod) {
  // auto mod = instr->getModule();
  auto datalayout = mod.getDataLayout();

  auto *llvm_type = alloca_instr->getAllocatedType();
  auto alloc_size = datalayout.getTypeAllocSize(llvm_type);
  ASSERT(!alloc_size.isScalable());
  auto type_size = alloc_size.getFixedValue();

  auto alloca = builder.build_alloca(
      foptim::fir::ValueR(fctx->get_constant_int(type_size, 32)));

  if (!llvm_type->isAggregateType()) {
    auto type = convert_type(llvm_type, fctx, mod);
    alloca.as_instr()->extra_type = type;
  }
  valueToValue.insert({any_instr, alloca});
}

void convert_gep(const llvm::Instruction *any_instr,
                 const llvm::GetElementPtrInst *gep_instr,
                 foptim::fir::Context &fctx, foptim::fir::FunctionR ffunc,
                 foptim::fir::Builder &builder, V2VMap &valueToValue,
                 llvm::Module &mod, B2BMap &b2b) {
  auto ptr = convert_instr_arg(gep_instr->getPointerOperand(), fctx, ffunc,
                               builder, valueToValue, mod, b2b);
  foptim::TVec<llvm::Value *> args = {};
  auto datalayout = mod.getDataLayout();
  auto result_value = ptr;
  auto *indexed_type = llvm::GetElementPtrInst::getIndexedType(
      gep_instr->getSourceElementType(), args);

  if (indexed_type->isStructTy() || indexed_type->isArrayTy()) {
    ASSERT(gep_instr->getNumIndices() >= 1);
    {  // first the index into the struct*
      auto offset_struct_ptr_foptim =
          convert_instr_arg(gep_instr->indices().begin()->get(), fctx, ffunc,
                            builder, valueToValue, mod, b2b);

      auto arg_mul_ptr = datalayout.getTypeAllocSize(indexed_type);

      ASSERT(arg_mul_ptr.isFixed())
      auto arg_mul_ptr_value =
          fctx->get_constant_int(arg_mul_ptr.getFixedValue(), 32);
      auto mul = builder.build_int_mul(offset_struct_ptr_foptim,
                                       foptim::fir::ValueR(arg_mul_ptr_value),
                                       true, true);
      result_value = builder.build_int_add(result_value, mul, true, true);
    }
    for (const auto *index_it = gep_instr->indices().begin() + 1;
         index_it != gep_instr->indices().end(); index_it++) {
      if (indexed_type->isStructTy()) {  // index into strut
        auto *struct_type =
            llvm::dyn_cast_or_null<llvm::StructType>(indexed_type);
        ASSERT(struct_type);

        auto offset_struct =
            llvm::dyn_cast_or_null<llvm::ConstantInt>(index_it->get())
                ->getZExtValue();
        auto arg_offset = datalayout.getStructLayout(struct_type)
                              ->getElementOffset(offset_struct);
        auto arg_offset_foptim =
            fctx->get_constant_int(arg_offset.getFixedValue(), 32);
        result_value = builder.build_int_add(
            result_value, foptim::fir::ValueR{arg_offset_foptim}, true, true);
        indexed_type = struct_type->getElementType(offset_struct);
      } else if (indexed_type->isArrayTy()) {  // index into array
        auto *array_type =
            llvm::dyn_cast_or_null<llvm::ArrayType>(indexed_type);
        ASSERT(array_type);
        auto offset_struct_ptr_foptim = convert_instr_arg(
            index_it->get(), fctx, ffunc, builder, valueToValue, mod, b2b);
        auto arg_mul_ptr =
            datalayout.getTypeAllocSize(array_type->getElementType());
        ASSERT(arg_mul_ptr.isFixed())
        auto arg_mul_ptr_value =
            fctx->get_constant_int(arg_mul_ptr.getFixedValue(), 32);
        auto mul = builder.build_int_mul(offset_struct_ptr_foptim,
                                         foptim::fir::ValueR(arg_mul_ptr_value),
                                         true, true);
        result_value = builder.build_int_add(result_value, mul, true, true);
        indexed_type = array_type->getElementType();
      }
    }
  } else {
    ASSERT(gep_instr->getNumIndices() == 1);
    auto arg_foptim =
        convert_instr_arg(gep_instr->indices().begin()->get(), fctx, ffunc,
                          builder, valueToValue, mod, b2b);
    auto arg_mul = datalayout.getTypeAllocSize(indexed_type);
    ASSERT(arg_mul.isFixed())
    auto arg_mul_value = fctx->get_constant_int(arg_mul.getFixedValue(), 32);
    auto mul = builder.build_int_mul(
        arg_foptim, foptim::fir::ValueR(arg_mul_value), true, true);
    result_value = builder.build_int_add(result_value, mul, true, true);
  }

  valueToValue.insert({any_instr, result_value});
}

void convert_branch(const llvm::BranchInst *branch_instr,
                    foptim::fir::Context &fctx, foptim::fir::FunctionR ffunc,
                    foptim::fir::Builder &builder, V2VMap &valueToValue,
                    llvm::Module &mod, B2BMap &b2b) {
  if (!branch_instr->isConditional()) {
    auto target = b2b.at(branch_instr->getSuccessor(0));
    builder.build_branch(target);
    return;
  }
  if (branch_instr->getNumSuccessors() == 2) {
    auto cond = convert_instr_arg(branch_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto target1 = b2b.at(branch_instr->getSuccessor(0));
    auto target2 = b2b.at(branch_instr->getSuccessor(1));
    builder.build_cond_branch(cond, target1, target2);
    return;
  }
  // UNREACH
}

void convert_switch(llvm::SwitchInst *switch_instr, foptim::fir::Context &fctx,
                    foptim::fir::FunctionR ffunc, foptim::fir::Builder &builder,
                    V2VMap &valueToValue, llvm::Module &mod, B2BMap &b2b) {
#if LLVM_VERSION_MAJOR < 21
  auto defaultDest =
      foptim::fir::BasicBlock{foptim::fir::BasicBlock::invalid()};
  if (!switch_instr->defaultDestUndefined()) {
    defaultDest = b2b.at(switch_instr->getDefaultDest());
  }
#else
  auto defaultDest = b2b.at(switch_instr->getDefaultDest());
#endif

  foptim::TVec<std::pair<foptim::fir::ConstantValueR, foptim::fir::BasicBlock>>
      cases;
  for (auto cass : switch_instr->cases()) {
    const auto *case_llvm_value = cass.getCaseValue();
    foptim::i128 val = 0;
    if (case_llvm_value->isNegative() && case_llvm_value->getBitWidth() != 1) {
      val = case_llvm_value->getSExtValue();
    } else {
      val = case_llvm_value->getZExtValue();
    }
    auto target_bb = b2b.at(cass.getCaseSuccessor());
    cases.emplace_back(
        fctx->get_constant_int(val, case_llvm_value->getBitWidth()), target_bb);
  }

  auto value = convert_instr_arg(switch_instr->getCondition(), fctx, ffunc,
                                 builder, valueToValue, mod, b2b);

  builder.build_switch(value, cases, defaultDest);
}

void convert_call(const llvm::Instruction *any_instr,
                  const llvm::CallInst *call_instr, foptim::fir::Context &fctx,
                  foptim::fir::FunctionR ffunc, foptim::fir::Builder &builder,
                  V2VMap &valueToValue, llvm::Module &mod, B2BMap &b2b) {
  foptim::TVec<foptim::fir::ValueR> args = {};
  if (const auto *c = call_instr->getCalledFunction()) {
    if (c->getName().starts_with("llvm.experimental.noalias.scope.decl")) {
      return;
    }
    if (c->getName() == "sqrt") {
      if (!c->hasFnAttribute(llvm::Attribute::NoBuiltin) ||
          c->hasFnAttribute(llvm::Attribute::Builtin)) {
        auto res = builder.build_unary_op(
            convert_instr_arg(call_instr->getOperand(0), fctx, ffunc, builder,
                              valueToValue, mod, b2b),
            foptim::fir::UnaryInstrSubType::FloatSqrt);
        valueToValue.insert({any_instr, res});
        return;
      }
    }
  }
  for (size_t i = 0; i < call_instr->getNumOperands() - 1; i++) {
    auto *arg = call_instr->getOperand(i);
    auto arg_foptim =
        convert_instr_arg(arg, fctx, ffunc, builder, valueToValue, mod, b2b);
    args.push_back(arg_foptim);
  }

  auto *ret_type = call_instr->getFunctionType()->getReturnType();
  auto func_type_foptim =
      convert_type(call_instr->getFunctionType(), fctx, mod);
  auto ret_type_foptim = convert_type(ret_type, fctx, mod);
  foptim::fir::ValueR res;

  auto function_ptr = convert_instr_arg(call_instr->getCalledOperand(), fctx,
                                        ffunc, builder, valueToValue, mod, b2b);
  res =
      builder.build_call(function_ptr, func_type_foptim, ret_type_foptim, args);
  // if (call_instr->isIndirectCall()) {
  // } else {
  //   res = builder.build_direct_call(
  //       call_instr->getCalledFunction()->getName().str(), func_type_foptim,
  //       ret_type_foptim, args);
  // }
  valueToValue.insert({any_instr, res});
}

bool convert_fcmp(const llvm::Instruction *any_instr,
                  const llvm::FCmpInst *cmp_inst, foptim::fir::Context &fctx,
                  foptim::fir::FunctionR ffunc, foptim::fir::Builder &builder,
                  V2VMap &valueToValue, llvm::Module &mod, B2BMap &b2b) {
  auto a = convert_instr_arg(cmp_inst->getOperand(0), fctx, ffunc, builder,
                             valueToValue, mod, b2b);
  auto b = convert_instr_arg(cmp_inst->getOperand(1), fctx, ffunc, builder,
                             valueToValue, mod, b2b);

  foptim::fir::FCmpInstrSubType pred = foptim::fir::FCmpInstrSubType::AlwTrue;
  switch (cmp_inst->getPredicate()) {
    case llvm::CmpInst::FCMP_FALSE:
      pred = foptim::fir::FCmpInstrSubType::AlwFalse;
      break;
    case llvm::CmpInst::FCMP_OEQ:
      pred = foptim::fir::FCmpInstrSubType::OEQ;
      break;
    case llvm::CmpInst::FCMP_OGT:
      pred = foptim::fir::FCmpInstrSubType::OGT;
      break;
    case llvm::CmpInst::FCMP_OGE:
      pred = foptim::fir::FCmpInstrSubType::OGE;
      break;
    case llvm::CmpInst::FCMP_OLT:
      pred = foptim::fir::FCmpInstrSubType::OLT;
      break;
    case llvm::CmpInst::FCMP_OLE:
      pred = foptim::fir::FCmpInstrSubType::OLE;
      break;
    case llvm::CmpInst::FCMP_ONE:
      pred = foptim::fir::FCmpInstrSubType::ONE;
      break;
    case llvm::CmpInst::FCMP_ORD:
      pred = foptim::fir::FCmpInstrSubType::ORD;
      break;
    case llvm::CmpInst::FCMP_UNO:
      pred = foptim::fir::FCmpInstrSubType::UNO;
      break;
    case llvm::CmpInst::FCMP_UEQ:
      pred = foptim::fir::FCmpInstrSubType::UEQ;
      break;
    case llvm::CmpInst::FCMP_UGT:
      pred = foptim::fir::FCmpInstrSubType::UGT;
      break;
    case llvm::CmpInst::FCMP_UGE:
      pred = foptim::fir::FCmpInstrSubType::UGE;
      break;
    case llvm::CmpInst::FCMP_ULT:
      pred = foptim::fir::FCmpInstrSubType::ULT;
      break;
    case llvm::CmpInst::FCMP_ULE:
      pred = foptim::fir::FCmpInstrSubType::ULE;
      break;
    case llvm::CmpInst::FCMP_UNE:
      pred = foptim::fir::FCmpInstrSubType::UNE;
      break;
    case llvm::CmpInst::FCMP_TRUE:
      pred = foptim::fir::FCmpInstrSubType::AlwTrue;
      break;
    case llvm::CmpInst::BAD_FCMP_PREDICATE: {
    }
    default:
      UNREACH();
  }
  auto res = builder.build_float_cmp(a, b, pred);
  valueToValue.insert({any_instr, res});
  return true;
}

bool convert_icmp(const llvm::Instruction *any_instr,
                  const llvm::ICmpInst *cmp_inst, foptim::fir::Context &fctx,
                  foptim::fir::FunctionR ffunc, foptim::fir::Builder &builder,
                  V2VMap &valueToValue, llvm::Module &mod, B2BMap &b2b) {
  auto a = convert_instr_arg(cmp_inst->getOperand(0), fctx, ffunc, builder,
                             valueToValue, mod, b2b);
  auto b = convert_instr_arg(cmp_inst->getOperand(1), fctx, ffunc, builder,
                             valueToValue, mod, b2b);
  foptim::fir::ICmpInstrSubType pred = foptim::fir::ICmpInstrSubType::EQ;
  switch (cmp_inst->getPredicate()) {
    case llvm::CmpInst::ICMP_UGE:
      pred = foptim::fir::ICmpInstrSubType::UGE;
      break;
    case llvm::CmpInst::ICMP_SLE:
      pred = foptim::fir::ICmpInstrSubType::SLE;
      break;
    case llvm::CmpInst::ICMP_ULE:
      pred = foptim::fir::ICmpInstrSubType::ULE;
      break;
    case llvm::CmpInst::ICMP_SGE:
      pred = foptim::fir::ICmpInstrSubType::SGE;
      break;
    case llvm::CmpInst::ICMP_EQ:
      pred = foptim::fir::ICmpInstrSubType::EQ;
      break;
    case llvm::CmpInst::ICMP_NE:
      pred = foptim::fir::ICmpInstrSubType::NE;
      break;
    case llvm::CmpInst::ICMP_UGT:
      pred = foptim::fir::ICmpInstrSubType::UGT;
      break;
    case llvm::CmpInst::ICMP_SGT:
      pred = foptim::fir::ICmpInstrSubType::SGT;
      break;
    case llvm::CmpInst::ICMP_ULT:
      pred = foptim::fir::ICmpInstrSubType::ULT;
      break;
    case llvm::CmpInst::ICMP_SLT:
      pred = foptim::fir::ICmpInstrSubType::SLT;
      break;
    default:
      UNREACH();
  }
  auto res = builder.build_int_cmp(a, b, pred);
  valueToValue.insert({any_instr, res});
  return true;
}

void convert(llvm::Instruction *any_instr, foptim::fir::Context &fctx,
             foptim::fir::FunctionR ffunc, foptim::fir::Builder &builder,
             V2VMap &valueToValue, llvm::Module &mod, B2BMap &b2b) {
  auto op_code = any_instr->getOpcode();
  if (const auto *instr = llvm::dyn_cast_or_null<llvm::ReturnInst>(any_instr)) {
    if (auto *v = instr->getReturnValue()) {
      auto fv =
          convert_instr_arg(v, fctx, ffunc, builder, valueToValue, mod, b2b);
      builder.build_return(fv);
    } else {
      builder.build_return();
    }
    return;
  }
  if (const auto *instr = llvm::dyn_cast_or_null<llvm::StoreInst>(any_instr)) {
    assert(instr->getNumOperands() == 2);
    auto value = convert_instr_arg(instr->getOperand(0), fctx, ffunc, builder,
                                   valueToValue, mod, b2b);
    auto ptr = convert_instr_arg(instr->getOperand(1), fctx, ffunc, builder,
                                 valueToValue, mod, b2b);
    auto store = builder.build_store(ptr, value, instr->isAtomic(), instr->isVolatile());
    valueToValue.insert({any_instr, store});
    return;
  }
  if (const auto *instr = llvm::dyn_cast_or_null<llvm::FPExtInst>(any_instr)) {
    assert(instr->getNumOperands() == 1);
    auto value = convert_instr_arg(instr->getOperand(0), fctx, ffunc, builder,
                                   valueToValue, mod, b2b);
    auto *dest_type = instr->getDestTy();
    auto conver =
        builder.build_conversion_op(value, convert_type(dest_type, fctx, mod),
                                    foptim::fir::ConversionSubType::FPEXT);
    valueToValue.insert({any_instr, conver});
    return;
  }
  if (const auto *instr =
          llvm::dyn_cast_or_null<llvm::FPTruncInst>(any_instr)) {
    assert(instr->getNumOperands() == 1);
    auto value = convert_instr_arg(instr->getOperand(0), fctx, ffunc, builder,
                                   valueToValue, mod, b2b);
    auto *dest_type = instr->getDestTy();
    auto conver =
        builder.build_conversion_op(value, convert_type(dest_type, fctx, mod),
                                    foptim::fir::ConversionSubType::FPTRUNC);
    valueToValue.insert({any_instr, conver});
    return;
  }
  if (const auto *instr = llvm::dyn_cast_or_null<llvm::LoadInst>(any_instr)) {
    assert(instr->getNumOperands() == 1);
    auto value = convert_instr_arg(instr->getOperand(0), fctx, ffunc, builder,
                                   valueToValue, mod, b2b);
    auto type = convert_type(instr->getAccessType(), fctx, mod);
    auto load = builder.build_load(type, value, instr->isAtomic(), instr->isVolatile());
    ASSERT(std::get<1>(valueToValue.insert({any_instr, load})));
    return;
  }
  if (const auto *instr = llvm::dyn_cast_or_null<llvm::SelectInst>(any_instr)) {
    assert(instr->getNumOperands() == 3);
    auto cond = convert_instr_arg(instr->getOperand(0), fctx, ffunc, builder,
                                  valueToValue, mod, b2b);
    auto v1 = convert_instr_arg(instr->getOperand(1), fctx, ffunc, builder,
                                valueToValue, mod, b2b);
    auto v2 = convert_instr_arg(instr->getOperand(2), fctx, ffunc, builder,
                                valueToValue, mod, b2b);
    auto type = convert_type(instr->getType(), fctx, mod);
    auto select = builder.build_select(type, cond, v1, v2);
    valueToValue.insert({any_instr, select});
    return;
  }
  if (const auto *instr = llvm::dyn_cast_or_null<llvm::AllocaInst>(any_instr)) {
    convert_alloca(any_instr, instr, fctx, builder, valueToValue, mod);
    return;
  }
  if (const auto *instr = llvm::dyn_cast_or_null<llvm::BranchInst>(any_instr)) {
    convert_branch(instr, fctx, ffunc, builder, valueToValue, mod, b2b);
    return;
  }
  if (auto *instr = llvm::dyn_cast_or_null<llvm::SwitchInst>(any_instr)) {
    convert_switch(instr, fctx, ffunc, builder, valueToValue, mod, b2b);
    return;
  }
  if (const auto *instr = llvm::dyn_cast_or_null<llvm::CallInst>(any_instr)) {
    convert_call(any_instr, instr, fctx, ffunc, builder, valueToValue, mod,
                 b2b);
    return;
  }
  if (const auto *instr =
          llvm::dyn_cast_or_null<llvm::GetElementPtrInst>(any_instr)) {
    convert_gep(any_instr, instr, fctx, ffunc, builder, valueToValue, mod, b2b);
    return;
  }
  if (const auto *instr = dyn_cast_or_null<llvm::TruncInst>(any_instr)) {
    auto arg = convert_instr_arg(instr->getOperand(0), fctx, ffunc, builder,
                                 valueToValue, mod, b2b);
    auto dest_ty = convert_type(instr->getDestTy(), fctx, mod);
    auto res = builder.build_itrunc(arg, dest_ty);
    valueToValue.insert({any_instr, res});
    return;
  }
  if (const auto *instr = dyn_cast_or_null<llvm::SExtInst>(any_instr)) {
    auto arg = convert_instr_arg(instr->getOperand(0), fctx, ffunc, builder,
                                 valueToValue, mod, b2b);
    auto dest_ty = convert_type(instr->getDestTy(), fctx, mod);
    auto res = builder.build_sext(arg, dest_ty);
    valueToValue.insert({any_instr, res});
    return;
  }
  if (const auto *instr = dyn_cast_or_null<llvm::ZExtInst>(any_instr)) {
    auto arg = convert_instr_arg(instr->getOperand(0), fctx, ffunc, builder,
                                 valueToValue, mod, b2b);
    auto dest_ty = convert_type(instr->getDestTy(), fctx, mod);
    auto res = builder.build_zext(arg, dest_ty);
    valueToValue.insert({any_instr, res});
    return;
  }
  if (op_code == llvm::Instruction::SIToFP ||
      op_code == llvm::Instruction::UIToFP ||
      op_code == llvm::Instruction::FPToSI ||
      op_code == llvm::Instruction::FPToUI) {
    auto arg = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc, builder,
                                 valueToValue, mod, b2b);
    auto dest_ty = convert_type(any_instr->getType(), fctx, mod);
    auto conversion = foptim::fir::ConversionSubType::SITOFP;
    if (op_code == llvm::Instruction::SIToFP) {
      conversion = foptim::fir::ConversionSubType::SITOFP;
    } else if (op_code == llvm::Instruction::UIToFP) {
      conversion = foptim::fir::ConversionSubType::UITOFP;
    } else if (op_code == llvm::Instruction::FPToSI) {
      conversion = foptim::fir::ConversionSubType::FPTOSI;
    } else if (op_code == llvm::Instruction::FPToUI) {
      conversion = foptim::fir::ConversionSubType::FPTOUI;
    } else {
      UNREACH();
    }
    auto res = builder.build_conversion_op(arg, dest_ty, conversion);
    valueToValue.insert({any_instr, res});
    return;
  }
  if (const auto *instr = llvm::dyn_cast_or_null<llvm::ICmpInst>(any_instr)) {
    if (convert_icmp(any_instr, instr, fctx, ffunc, builder, valueToValue, mod,
                     b2b)) {
      return;
    }
  } else if (const auto *instr =
                 llvm::dyn_cast_or_null<llvm::FCmpInst>(any_instr)) {
    if (convert_fcmp(any_instr, instr, fctx, ffunc, builder, valueToValue, mod,
                     b2b)) {
      return;
    }
  } else if (op_code == llvm::Instruction::SRem) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add = builder.build_int_srem(left, right);
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::URem) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add = builder.build_int_urem(left, right);
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::Add) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add =
          builder.build_int_add(left, right, any_instr->hasNoUnsignedWrap(),
                                any_instr->hasNoSignedWrap());
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::SDiv) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add = builder.build_binary_op(
          left, right, foptim::fir::BinaryInstrSubType::IntSDiv);
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::UDiv) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add = builder.build_binary_op(
          left, right, foptim::fir::BinaryInstrSubType::IntUDiv);
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::Shl) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add = builder.build_shl(left, right);
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::AShr) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add = builder.build_ashr(left, right);
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::LShr) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add = builder.build_lshr(left, right);
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::Sub) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add = builder.build_int_sub(left, right);
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::Mul) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add =
          builder.build_int_mul(left, right, any_instr->hasNoUnsignedWrap(),
                                any_instr->hasNoSignedWrap());
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::And) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add = builder.build_binary_op(left, right,
                                         foptim::fir::BinaryInstrSubType::And);
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::Or) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add = builder.build_binary_op(left, right,
                                         foptim::fir::BinaryInstrSubType::Or);
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::Xor) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add = builder.build_binary_op(left, right,
                                         foptim::fir::BinaryInstrSubType::Xor);
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::FAdd) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isFloatingPointTy()) {
      auto add = builder.build_float_add(left, right);
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::FSub) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isFloatingPointTy()) {
      auto add = builder.build_float_sub(left, right);
      valueToValue.insert({any_instr, add});
      return;
    }
  } else if (op_code == llvm::Instruction::FMul) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    ASSERT(any_instr->getType()->isFloatingPointTy());
    auto add = builder.build_float_mul(left, right);
    valueToValue.insert({any_instr, add});
    return;
  } else if (op_code == llvm::Instruction::FDiv) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    ASSERT(any_instr->getType()->isFloatingPointTy());
    auto add = builder.build_float_div(left, right);
    valueToValue.insert({any_instr, add});
    return;
  } else if (op_code == llvm::Instruction::FNeg) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    ASSERT(any_instr->getType()->isFloatingPointTy());
    auto add =
        builder.build_unary_op(left, foptim::fir::UnaryInstrSubType::FloatNeg);
    valueToValue.insert({any_instr, add});
    return;
  } else if (op_code == llvm::Instruction::PHI) {
    auto ftype = convert_type(any_instr->getType(), fctx, mod);
    auto curr_bb = builder.get_curr_bb();
    auto new_arg = curr_bb.add_arg(fctx->storage.insert_bb_arg(curr_bb, ftype));
    valueToValue.insert({any_instr, foptim::fir::ValueR{new_arg}});
    return;
    return;
  } else if (op_code == llvm::Instruction::PtrToInt) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto add = builder.build_conversion_op(
        left, fctx->get_int_type(64), foptim::fir::ConversionSubType::PtrToInt);
    valueToValue.insert({any_instr, add});
    return;
  } else if (op_code == llvm::Instruction::IntToPtr) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto add = builder.build_conversion_op(
        left, fctx->get_ptr_type(), foptim::fir::ConversionSubType::IntToPtr);
    valueToValue.insert({any_instr, add});
    return;
  } else if (auto *instr =
                 llvm::dyn_cast_or_null<llvm::ExtractValueInst>(any_instr)) {
    auto stru = convert_instr_arg(instr->getAggregateOperand(), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto indicies = instr->getIndices();
    auto *out_type = instr->getType();
    foptim::TVec<foptim::fir::ValueR> args;
    for (auto index : indicies) {
      args.emplace_back(fctx->get_constant_int(index, 32));
    }
    auto add = builder.build_extract_value(stru, args,
                                           convert_type(out_type, fctx, mod));
    valueToValue.insert({any_instr, add});
    return;
  } else if (auto *instr =
                 llvm::dyn_cast_or_null<llvm::ExtractElementInst>(any_instr)) {
    auto indx = convert_instr_arg(instr->getIndexOperand(), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto val = convert_instr_arg(instr->getVectorOperand(), fctx, ffunc,
                                 builder, valueToValue, mod, b2b);
    auto *out_type = instr->getType();
    foptim::TVec<foptim::fir::ValueR> args;
    args.emplace_back(indx);
    auto add = builder.build_extract_value(val, args,
                                           convert_type(out_type, fctx, mod));
    valueToValue.insert({any_instr, add});
    return;
  } else if (auto *instr =
                 llvm::dyn_cast_or_null<llvm::InsertValueInst>(any_instr)) {
    auto stru = convert_instr_arg(instr->getAggregateOperand(), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto v = convert_instr_arg(instr->getInsertedValueOperand(), fctx, ffunc,
                               builder, valueToValue, mod, b2b);
    auto indicies = instr->getIndices();
    auto *out_type = instr->getType();
    foptim::TVec<foptim::fir::ValueR> args;
    for (auto index : indicies) {
      args.emplace_back(fctx->get_constant_int(index, 32));
    }
    auto add = builder.build_insert_value(stru, v, args,
                                          convert_type(out_type, fctx, mod));
    valueToValue.insert({any_instr, add});
    return;
  } else if (op_code == llvm::Instruction::Unreachable) {
    // TODO: unraech instr?
    builder.build_unreach();
    return;
  }
  llvm::errs() << *any_instr << "\n" << "TODO\n";
  TODO("");
  std::abort();
}

bool convert_decl(llvm::Function &func, foptim::fir::Context &fctx,
                  V2VMap &valueToValue, llvm::Module &mod) {
  if (func.getName().starts_with("llvm.memset")) {
    generate_memset(fctx);
  } else if (func.getName().starts_with("llvm.memcpy")) {
    generate_memcpy(fctx);
  } else if (func.getName().starts_with("llvm.trap")) {
    generate_trap(fctx);
  } else if (func.getName().starts_with("llvm.fabs") ||
             func.getName().starts_with("llvm.abs")) {
  } else if (func.getName().starts_with("llvm.exp.f")) {
    generate_fexp(fctx);
  } else if (func.getName().starts_with("llvm.memmove")) {
    generate_memmove(fctx);
  } else if (func.getName().starts_with(
                 "llvm.experimental.noalias.scope.decl")) {
    return false;
  }
  foptim::IRString func_name = func.getName().str().c_str();
  fctx.data->storage.functions.insert(
      {func_name, std::make_unique<foptim::fir::Function>(
                      fctx.operator->(), func_name,
                      convert_type(func.getFunctionType(), fctx, mod))});

  const auto foff_func = fctx->get_function(func_name.c_str());
  const auto func_ptr = fctx->get_constant_value(foff_func);
  valueToValue.insert({&func, foptim::fir::ValueR{func_ptr}});
  return true;
}

void setup_function(llvm::Function &func, foptim::fir::Context &fctx,
                    V2VMap &valueToValue, llvm::Module &mod) {
  if (!func.hasName()) {
    return;
  }

  foptim::IRString func_name = func.getName().str().c_str();

  if (func.empty()) {
    if (!convert_decl(func, fctx, valueToValue, mod)) {
      return;
    }
  } else {
    auto foff_ftype = convert_type(func.getFunctionType(), fctx, mod);
    auto foff_func = fctx->create_function(func_name, foff_ftype);
    auto func_ptr = fctx->get_constant_value(foff_func);
    valueToValue.insert({&func, foptim::fir::ValueR{func_ptr}});
    {
      auto entry = foff_func->get_entry();
      for (size_t i = 0; i < func.arg_size(); i++) {
        auto *const la = func.getArg(i);
        auto fa = entry->args[i];
        if (la->hasNoAliasAttr()) {
          fa->noalias = true;
        }
      }
    }
  }

  const auto foff_func = fctx->get_function(func.getName().str().c_str());
  if (func.isVarArg()) {
    foff_func->variadic = true;
  }

  switch (func.getCallingConv()) {
    case llvm::CallingConv::Fast:
      fmt::print(
          fg(fmt::color::red),
          "[WARNING] Unsupported cc fast cc will still try to run with C cc\n");
    case llvm::CallingConv::C:
      foff_func->cc = foptim::fir::Function::CallingConv::C;
      break;
    default:
      llvm::errs() << "Not supporting calling convention:"
                   << func.getCallingConv() << "\n";
      TODO("");
  }

  switch (func.getLinkage()) {
    case llvm::GlobalValue::InternalLinkage:
    case llvm::GlobalValue::PrivateLinkage:
      foff_func->linkage = foptim::fir::Linkage::Internal;
      break;
    case llvm::GlobalValue::LinkOnceAnyLinkage:
      foff_func->linkage = foptim::fir::Linkage::LinkOnce;
      break;
    case llvm::GlobalValue::LinkOnceODRLinkage:
      foff_func->linkage = foptim::fir::Linkage::LinkOnceODR;
      break;
    case llvm::GlobalValue::WeakAnyLinkage:
      foff_func->linkage = foptim::fir::Linkage::Weak;
      break;
    case llvm::GlobalValue::WeakODRLinkage:
      foff_func->linkage = foptim::fir::Linkage::WeakODR;
      break;
    case llvm::GlobalValue::ExternalLinkage:
    case llvm::GlobalValue::AvailableExternallyLinkage:
    case llvm::GlobalValue::AppendingLinkage:
    case llvm::GlobalValue::ExternalWeakLinkage:
    case llvm::GlobalValue::CommonLinkage:
      foff_func->linkage = foptim::fir::Linkage::External;
      break;
  }
  switch (func.getVisibility()) {
    case llvm::GlobalValue::DefaultVisibility:
      foff_func->linkvis = foptim::fir::LinkVisibility::Default;
      break;
    case llvm::GlobalValue::HiddenVisibility:
      foff_func->linkvis = foptim::fir::LinkVisibility::Hidden;
      break;
    case llvm::GlobalValue::ProtectedVisibility:
      foff_func->linkvis = foptim::fir::LinkVisibility::Protected;
      break;
  }
  if (foptim::utils::all_linkage_internal && func_name != "main" &&
      !func.empty()) {
    foff_func->linkage = foptim::fir::Linkage::Internal;
  }

  foff_func->no_inline =
      func.hasFnAttribute(llvm::Attribute::AttrKind::NoInline);
  foff_func->must_inline =
      func.hasFnAttribute(llvm::Attribute::AttrKind::AlwaysInline);
  foff_func->must_progress = func.mustProgress();
  // readNone: 0, readOnly: 0,
  // noInline: 0, alwaysInline:
  foff_func->no_recurse = func.doesNotRecurse();
  foff_func->no_inline =
      func.hasFnAttribute(llvm::Attribute::AttrKind::NoReturn);

  if (func.doesNotAccessMemory()) {
    foff_func->mem_read_none = true;
  } else if (func.onlyReadsMemory()) {
    foff_func->mem_read_only = true;
  }
}

void convert(llvm::Function &func, foptim::fir::Context &fctx,
             V2VMap &valueToValue) {
  ZoneScopedN("Convert Func");
  if (!func.hasName() || func.empty()) {
    return;
  }
  // if () {
  //   return convert_decl(func, fctx);
  // }

  auto name = func.getName();
  auto ffunc = fctx->get_function(name.str().c_str());

  // func.getMemoryEffects()
  // func.isConvergent()

  auto fbuilder = ffunc.builder();

  llvm::BasicBlock &entry_bb = func.getEntryBlock();

  foptim::TSet<llvm::BasicBlock *> visited_bbs;
  std::deque<std::pair<llvm::BasicBlock *, foptim::fir::BasicBlock>> worklist{
      {&entry_bb, ffunc->get_entry()}};

  B2BMap b2b{};
  auto fentry_bb = ffunc->get_entry();
  b2b.insert({&entry_bb, fentry_bb});

  auto &fargs = fentry_bb->get_args();
  for (size_t arg_idx = 0; arg_idx < fargs.size(); arg_idx++) {
    valueToValue.insert(
        {func.getArg(arg_idx), foptim::fir::ValueR(fentry_bb->args[arg_idx])});
  }

  // FIXME: implement this
  //     The issue is that the initial bbs arguemtns in this IR are the
  //     function args
  //       so we need to insert an aditional bb prior that handles those so
  //       the actual entry then can handle these phis
  ASSERT(entry_bb.phis().begin() == entry_bb.phis().end());

  while (!worklist.empty()) {
    auto [bb_llvm, bb_foptim] = worklist.front();
    worklist.pop_front();
    // if(visited_bbs.contains(bb_llvm)){
    //   //added it twice to q
    //   continue;
    // }

    fbuilder.at_end(bb_foptim);
    bb_foptim->instructions.reserve(bb_llvm->size());

    for (auto &instr : *bb_llvm) {
      // if its the terminator we need to first create the new bbs
      //  it might branch to and also add them to the worklist before
      //  actually handling it
      if (instr.isTerminator()) {
        auto n_sucs = instr.getNumSuccessors();
        for (u32 i = 0; i < n_sucs; i++) {
          auto *succ = instr.getSuccessor(i);
          if (!visited_bbs.contains(succ)) {
            auto new_bb = fbuilder.append_bb();
            b2b.insert({succ, new_bb});
            worklist.emplace_back(succ, new_bb);
            visited_bbs.insert(succ);
          }
        }
      }
      convert(&instr, fctx, ffunc, fbuilder, valueToValue, *func.getParent(),
              b2b);
    }
  }

  // now we already generated all bbs and their arguments but not the actual
  // phi arguments -> to branch arguments so we need to fix em up
  {
    // so we iterate over all phis and their arguments find their
    // corresponding fbb and there insert as bb arg
    //  the phi arg
    foptim::fir::Builder build{ffunc.func->basic_blocks[0]};
    for (auto &bb : func) {
      if (!b2b.contains(&bb)) {
        continue;
      }
      auto to_fbb = b2b.at(&bb);
      for (auto &phi : bb.phis()) {
        for (size_t phi_arg_id = 0; phi_arg_id < phi.getNumIncomingValues();
             ++phi_arg_id) {
          auto from_fbb = b2b.at(phi.getIncomingBlock(phi_arg_id));
          build.at_penultimate(from_fbb);

          auto value =
              convert_instr_arg(phi.getIncomingValue(phi_arg_id), fctx, ffunc,
                                build, valueToValue, *func.getParent(), b2b);
          // auto value = valueToValue.at(phi.getIncomingValue(phi_arg_id));

          auto term = from_fbb->get_terminator();
          auto to_bb_id = term.get_bb_id(to_fbb);
          term.add_bb_arg(to_bb_id, value);
        }
      }
    }
  }
}

void convert_constant_init(const uint8_t *output, const llvm::Constant *val,
                           foptim::fir::Context &fctx, foptim::fir::Global glob,
                           llvm::DataLayout &layout, V2VMap &valueToValue) {
  if ((llvm::dyn_cast_or_null<llvm::ConstantAggregateZero>(val) != nullptr) ||
      (llvm::dyn_cast_or_null<llvm::UndefValue>(val) != nullptr) ||
      (llvm::dyn_cast_or_null<llvm::ConstantPointerNull>(val) != nullptr)) {
    // Dont need to do anything here
    return;
  }
  if (const auto *d =
          llvm::dyn_cast_or_null<llvm::ConstantDataSequential>(val)) {
    size_t offset = 0;
    for (size_t i = 0; i < d->getNumElements(); i++) {
      auto *sub_value = d->getElementAsConstant(i);
      convert_constant_init(&output[offset], sub_value, fctx, glob, layout,
                            valueToValue);
      // TODO: this offset might be wrong?
      offset += d->getElementByteSize();
    }
    return;
  }
  if (const auto *d = llvm::dyn_cast_or_null<llvm::ConstantInt>(val)) {
    switch (d->getBitWidth()) {
      case 8:
        *((uint8_t *)output) = (uint8_t)d->getZExtValue();
        break;
      case 16:
        *((uint16_t *)output) = (uint16_t)d->getZExtValue();
        break;
      case 32:
        *((uint32_t *)output) = (uint32_t)d->getZExtValue();
        break;
      case 64:
        *((uint64_t *)output) = (uint64_t)d->getZExtValue();
        break;
      default:
        llvm::errs() << "constant int " << *d << " " << d->getBitWidth()
                     << "\n";
        TODO("IMPL");
    }
    return;
  }
  if (const auto *d = llvm::dyn_cast_or_null<llvm::ConstantFP>(val)) {
    auto ty_id = d->getType()->getTypeID();
    if (ty_id == llvm::Type::FloatTyID) {
      auto val = (u32)d->getValue().bitcastToAPInt().getZExtValue();
      *((uint32_t *)output) = (uint32_t)val;
    } else if (ty_id == llvm::Type::DoubleTyID) {
      auto val = (u64)d->getValue().bitcastToAPInt().getZExtValue();
      *((u64 *)output) = val;
    } else {
      llvm::errs() << "TODO: handle global init\n";
      llvm::errs() << "constant fp " << *d << "\n";
      TODO("IMPL");
    }
    return;
  }
  if (const auto *d = llvm::dyn_cast_or_null<llvm::ConstantStruct>(val)) {
    auto *struct_ty = d->getType();
    const auto *str_layout = layout.getStructLayout(struct_ty);

    for (size_t m = 0; m < d->getNumOperands(); m++) {
      auto *sub_value = d->getOperand(m);
      auto offset = str_layout->getElementOffset(m);
      convert_constant_init(&output[offset], sub_value, fctx, glob, layout,
                            valueToValue);
    }
    return;
  }
  if (const auto *d = llvm::dyn_cast_or_null<llvm::ConstantArray>(val)) {
    auto member_size = layout.getTypeAllocSize(d->getType()->getElementType());

    size_t offset = 0;
    for (size_t m = 0; m < d->getNumOperands(); m++) {
      auto *sub_value = d->getOperand(m);
      convert_constant_init(&output[offset], sub_value, fctx, glob, layout,
                            valueToValue);
      offset += member_size;
    }
    return;
  }
  if (const auto *d = llvm::dyn_cast_or_null<llvm::GlobalVariable>(val)) {
    size_t reloc_off = output - glob->init_value;
    foptim::fir::ConstantValueR reloc_ref = valueToValue.at(d).as_constant();
    glob->reloc_info.push_back({reloc_off, reloc_ref});
    return;
  }
  if (const auto *d = llvm::dyn_cast_or_null<llvm::ConstantExpr>(val)) {
    if (const auto *gep = llvm::dyn_cast_or_null<llvm::GetElementPtrInst>(
            d->getAsInstruction())) {
      ASSERT(gep->getNumIndices() == 1);
      ASSERT(!gep->getSourceElementType()->isIntegerTy() ||
             gep->getSourceElementType()->getIntegerBitWidth() == 8);
      ASSERT((gep->getSourceElementType()->isPointerTy() ||
              gep->getSourceElementType()->isIntegerTy()));

      auto *const index = gep->indices().begin()->get();
      if (const auto *int_constant =
              llvm::dyn_cast_or_null<llvm::ConstantInt>(index)) {
        u32 bitwidth = int_constant->getBitWidth();
        auto value = int_constant->getValue();
        foptim::i128 val = 0;

        if (value.isNegative() && bitwidth != 1) {
          val = value.getSExtValue();
        } else {
          val = value.getZExtValue();
        }
        ASSERT(val > 0 && val < std::numeric_limits<u64>::max());

        size_t reloc_off = output - glob->init_value;
        foptim::fir::ConstantValueR reloc_ref =
            foptim::fir::ConstantValueR{foptim::fir::ConstantValueR::invalid()};
        if (valueToValue.contains(gep->getPointerOperand())) {
          reloc_ref = valueToValue.at(gep->getPointerOperand()).as_constant();
        } else if (const auto *func = llvm::dyn_cast_or_null<llvm::Function>(
                       gep->getPointerOperand())) {
          reloc_ref = fctx->get_constant_value(
              fctx->get_function(func->getName().str().c_str()));
        } else {
          TODO("impl");
        }
        glob->reloc_info.push_back({reloc_off, reloc_ref, (u64)val});
        return;
      }

      llvm::errs() << *d << "\n";
      TODO("OKAK");
    }
    llvm::errs() << *d << "\n";
    TODO("IMPL CONSTANT EXPR");
    // size_t reloc_off = output - glob->init_value;
    // foptim::fir::ConstantValueR reloc_ref =
    // valueToValue.at(d).as_constant();
    // glob->reloc_info.push_back({reloc_off, reloc_ref});
    return;
  }
  if (const auto *d = llvm::dyn_cast_or_null<llvm::Function>(val)) {
    size_t reloc_off = output - glob->init_value;
    // llvm::errs() << "getting func" << (int)valueToValue.contains(d) << " "
    //              << d->getName() << "\n";
    foptim::fir::ConstantValueR reloc_ref = valueToValue.at(d).as_constant();
    glob->reloc_info.push_back({reloc_off, reloc_ref});
    return;
  }
  if (const auto *d = llvm::dyn_cast_or_null<llvm::GlobalAlias>(val)) {
    llvm::errs() << "TODO: handle global init\n";
    llvm::errs() << "idk " << *val << "\n";
    llvm::errs() << "idk " << *d->getAliasee() << "\n";
    llvm::errs() << "idk " << *d->getAliaseeObject() << "\n";
    TODO("im");
  }
  llvm::errs() << "TODO: handle global init\n";
  llvm::errs() << "idk " << *val << "\n";
  llvm::errs() << "isConstantData: "
               << static_cast<int>(llvm::dyn_cast_or_null<llvm::ConstantData>(
                                       val) != nullptr)
               << "\n";
  llvm::errs() << "isConstantExpr: "
               << static_cast<int>(llvm::dyn_cast_or_null<llvm::ConstantExpr>(
                                       val) != nullptr)
               << "\n";
  llvm::errs() << "isGlobalValue: "
               << static_cast<int>(
                      llvm::dyn_cast_or_null<llvm::GlobalValue>(val) != nullptr)
               << "\n";
  llvm::errs() << "isConstantAggregate: "
               << static_cast<int>(
                      llvm::dyn_cast_or_null<llvm::ConstantAggregate>(val) !=
                      nullptr)
               << "\n";
  TODO("IMPL");
}

inline void setup_global(llvm::Module &mod, llvm::GlobalValue &gval,
                         foptim::fir::Context &fctx, V2VMap &valueToValue) {
  if (const auto *val = dyn_cast_or_null<llvm::GlobalVariable>(&gval)) {
    auto data_layout = mod.getDataLayout();
    auto global_size = data_layout.getTypeAllocSize(val->getValueType());
    ASSERT(!global_size.isScalable());

    auto actual_size = global_size.getFixedValue();

    foptim::IRString name;
    if (gval.hasName()) {
      name = gval.getName().str().c_str();
    } else {
      name = "it didnt have a name??";
    }

    auto global = fctx->insert_global(name, actual_size);
    auto as_global = fctx->get_constant_value(global);

    switch (val->getLinkage()) {
      case llvm::GlobalValue::ExternalLinkage:
      case llvm::GlobalValue::AvailableExternallyLinkage:
      case llvm::GlobalValue::AppendingLinkage:
      case llvm::GlobalValue::CommonLinkage:
        global->linkage = foptim::fir::Linkage::External;
        break;
      case llvm::GlobalValue::LinkOnceAnyLinkage:
        global->linkage = foptim::fir::Linkage::LinkOnce;
        break;
      case llvm::GlobalValue::LinkOnceODRLinkage:
        global->linkage = foptim::fir::Linkage::LinkOnceODR;
        break;
      case llvm::GlobalValue::ExternalWeakLinkage:
      case llvm::GlobalValue::WeakAnyLinkage:
        global->linkage = foptim::fir::Linkage::Weak;
        break;
      case llvm::GlobalValue::WeakODRLinkage:
        global->linkage = foptim::fir::Linkage::WeakODR;
        break;
      case llvm::GlobalValue::InternalLinkage:
      case llvm::GlobalValue::PrivateLinkage:
        global->linkage = foptim::fir::Linkage::Internal;
        break;
    }
    switch (val->getVisibility()) {
      case llvm::GlobalValue::DefaultVisibility:
        global->linkvis = foptim::fir::LinkVisibility::Default;
        break;
      case llvm::GlobalValue::HiddenVisibility:
        global->linkvis = foptim::fir::LinkVisibility::Hidden;
        break;
      case llvm::GlobalValue::ProtectedVisibility:
        global->linkvis = foptim::fir::LinkVisibility::Protected;
        break;
    }
    global->is_constant = val->isConstant();
    if (!val->isDeclaration()) {
      global->init_value =
          foptim::utils::IRAlloc<uint8_t>{}.allocate(actual_size);
      memset(global->init_value, 0, actual_size);
    }
    valueToValue.insert({(llvm::Value *)&gval, foptim::fir::ValueR(as_global)});
  }
}

void convert(llvm::Module &mod, llvm::GlobalValue &gval,
             foptim::fir::Context &fctx, V2VMap &valueToValue) {
  if (const auto *val = dyn_cast_or_null<llvm::GlobalVariable>(&gval)) {
    ZoneScopedN("Initializing Global Variable");
    auto layout = mod.getDataLayout();
    if (val->hasInitializer()) {
      auto global = valueToValue.at(val).as_constant()->as_global();
      convert_constant_init(global->init_value, val->getInitializer(), fctx,
                            global, layout, valueToValue);
    }
  } else {
    // llvm::errs() << "Not handling global " << gval;
  }
}

void convert(llvm::Module &mod, foptim::fir::Context &fctx,
             foptim::JobSheduler &shed) {
  V2VMap valueToValue;
  {
    ZoneScopedN("Initializing Globals + Functions");
    for (auto &globals : mod.global_values()) {
      setup_global(mod, globals, fctx, valueToValue);
    }
    for (auto &func : mod.functions()) {
      setup_function(func, fctx, valueToValue, mod);
    }
  }
  {
    ZoneScopedN("Populating Globals + Functions");
    for (auto &globals : mod.global_values()) {
      convert(mod, globals, fctx, valueToValue);
    }
    for (auto &func : mod.functions()) {
      shed.push(nullptr, [&func, &fctx, &valueToValue]() {
        auto v = foptim::utils::TempAlloc<void *>::save();
        V2VMap v2v_map_copy = valueToValue;
        convert(func, fctx, v2v_map_copy);
        foptim::utils::TempAlloc<void *>::restore(v);
      });
      // convert(func, fctx, valueToValue);
    }
    shed.wait_till_done();
  }
}

}  // namespace

void load_llvm_ir(const char *filename, foptim::fir::Context &fctx,
                  foptim::JobSheduler &shed) {
  llvm::LLVMContext context;
  llvm::SMDiagnostic error;
  std::unique_ptr<llvm::Module> module;
  {
    ZoneScopedN("LLVM");
    module = llvm::parseIRFile(filename, error, context);
  }
  if (module) {
    convert(*module, fctx, shed);
  } else {
    llvm::errs() << "FAILED TO LOAD: '" << filename << "' "
                 << error.getMessage() << "\n";
  }
}
