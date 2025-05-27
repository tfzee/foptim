#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/context.hpp"
#include "ir/function.hpp"
#include "ir/function_ref.hpp"
#include "ir/global.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "ir/value.hpp"
#include "utils/arena.hpp"
#include "utils/parameters.hpp"
#include "utils/set.hpp"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <cstdlib>
#include <deque>
#include <limits>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <memory>

using foptim::u32;
using foptim::u64;

using V2VMap = foptim::TMap<const llvm::Value *, foptim::fir::ValueR>;
using B2BMap = foptim::TMap<llvm::BasicBlock *, foptim::fir::BasicBlock>;

inline void convert(llvm::Instruction *any_instr, foptim::fir::Context &fctx,
                    foptim::fir::FunctionR ffunc, foptim::fir::Builder &builder,
                    V2VMap &valueToValue, llvm::Module &mod, B2BMap &b2b);

inline foptim::fir::ValueR
convert_instr_arg(const llvm::Value *value, foptim::fir::Context &fctx,
                  foptim::fir::FunctionR ffunc, foptim::fir::Builder &builder,
                  V2VMap &valueToValue, llvm::Module &mod, B2BMap &b2b) {
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
      return foptim::fir::ValueR(fctx->get_constant_value(
          value.getSExtValue(), fctx->get_int_type(bitwidth)));
    }
    return foptim::fir::ValueR(fctx->get_constant_value(
        value.getZExtValue(), fctx->get_int_type(bitwidth)));
  }
  if (const auto *ptr_null_constant =
          llvm::dyn_cast_or_null<llvm::ConstantPointerNull>(value)) {
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
  if (const auto *func = llvm::dyn_cast_or_null<llvm::Function>(value)) {
    return foptim::fir::ValueR(fctx->get_constant_value(
        fctx->get_function(value->getName().str().c_str())));
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

inline foptim::fir::TypeR convert_type(llvm::Type *any_ty,
                                       foptim::fir::Context &ctx,
                                       llvm::Module &module) {
  if (auto *v = llvm::dyn_cast_or_null<llvm::IntegerType>(any_ty)) {
    u32 width = v->getBitWidth();
    return ctx->get_int_type(width);
  }
  if (any_ty->isFloatTy()) {
    return ctx->get_float_type(32);
  }
  if (any_ty->isDoubleTy()) {
    return ctx->get_float_type(64);
  }
  if (llvm::dyn_cast_or_null<llvm::PointerType>(any_ty) != nullptr) {
    return ctx->get_ptr_type();
  }
  if (any_ty->isVoidTy()) {
    return ctx->get_void_type();
  }
  if (auto *stru = llvm::dyn_cast_or_null<llvm::StructType>(any_ty)) {
    foptim::IRVec<foptim::fir::StructType::StructElem> elems;
    for (u32 member_id = 0; member_id < stru->getStructNumElements();
         member_id++) {
      const auto *struct_layout = module.getDataLayout().getStructLayout(stru);
      auto offset = struct_layout->getElementOffset(member_id);
      auto ty =
          convert_type(stru->getStructElementType(member_id), ctx, module);
      elems.push_back({offset, ty});
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

  llvm::errs() << "FAILED TO CONVERT LLVM IR TYPE TO NORMAL TYPE TODO\n";
  llvm::errs() << *any_ty << "\n" << "TODO\n";
  std::abort();
}

inline void convert_alloca(const llvm::Instruction *any_instr,
                           const llvm::AllocaInst *alloca_instr,
                           foptim::fir::Context &fctx,
                           foptim::fir::Builder &builder, V2VMap &valueToValue,
                           llvm::Module &mod) {

  // auto mod = instr->getModule();
  auto datalayout = mod.getDataLayout();

  auto *llvm_type = alloca_instr->getAllocatedType();
  auto alloc_size = datalayout.getTypeAllocSize(llvm_type);
  ASSERT(!alloc_size.isScalable());
  auto type_size = alloc_size.getFixedValue();

  auto alloca = builder.build_alloca(foptim::fir::ValueR(
      fctx->get_constant_value(type_size, fctx->get_int_type(32))));

  if (!llvm_type->isAggregateType()) {
    auto type = convert_type(llvm_type, fctx, mod);
    alloca.as_instr()->add_attrib("alloca::type", type);
  }
  valueToValue.insert({any_instr, alloca});
}

inline void convert_gep(const llvm::Instruction *any_instr,
                        const llvm::GetElementPtrInst *gep_instr,
                        foptim::fir::Context &fctx,
                        foptim::fir::FunctionR ffunc,
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
    { // first the index into the struct*
      auto offset_struct_ptr_foptim =
          convert_instr_arg(gep_instr->indices().begin()->get(), fctx, ffunc,
                            builder, valueToValue, mod, b2b);

      auto arg_mul_ptr = datalayout.getTypeAllocSize(indexed_type);

      ASSERT(arg_mul_ptr.isFixed())
      auto arg_mul_ptr_value = fctx->get_constant_value(
          arg_mul_ptr.getFixedValue(), fctx->get_int_type(32));
      auto mul = builder.build_int_mul(offset_struct_ptr_foptim,
                                       foptim::fir::ValueR(arg_mul_ptr_value));
      result_value = builder.build_int_add(result_value, mul);
    }
    for (const auto *index_it = gep_instr->indices().begin() + 1;
         index_it != gep_instr->indices().end(); index_it++) {
      if (indexed_type->isStructTy()) { // index into strut
        auto *struct_type =
            llvm::dyn_cast_or_null<llvm::StructType>(indexed_type);
        ASSERT(struct_type);

        auto offset_struct =
            llvm::dyn_cast_or_null<llvm::ConstantInt>(index_it->get())
                ->getZExtValue();
        auto arg_offset = datalayout.getStructLayout(struct_type)
                              ->getElementOffset(offset_struct);
        auto arg_offset_foptim = fctx->get_constant_value(
            arg_offset.getFixedValue(), fctx->get_int_type(32));
        result_value = builder.build_int_add(
            result_value, foptim::fir::ValueR{arg_offset_foptim});
        indexed_type = struct_type->getElementType(offset_struct);
      } else if (indexed_type->isArrayTy()) { // index into array
        auto *array_type =
            llvm::dyn_cast_or_null<llvm::ArrayType>(indexed_type);
        ASSERT(array_type);
        auto offset_struct_ptr_foptim = convert_instr_arg(
            index_it->get(), fctx, ffunc, builder, valueToValue, mod, b2b);
        auto arg_mul_ptr =
            datalayout.getTypeAllocSize(array_type->getElementType());
        ASSERT(arg_mul_ptr.isFixed())
        auto arg_mul_ptr_value = fctx->get_constant_value(
            arg_mul_ptr.getFixedValue(), fctx->get_int_type(32));
        auto mul = builder.build_int_mul(
            offset_struct_ptr_foptim, foptim::fir::ValueR(arg_mul_ptr_value));
        result_value = builder.build_int_add(result_value, mul);
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
    auto arg_mul_value = fctx->get_constant_value(arg_mul.getFixedValue(),
                                                  fctx->get_int_type(32));
    auto mul =
        builder.build_int_mul(arg_foptim, foptim::fir::ValueR(arg_mul_value));
    result_value = builder.build_int_add(result_value, mul);
  }

  valueToValue.insert({any_instr, result_value});
}

inline void convert_branch(const llvm::BranchInst *branch_instr,
                           foptim::fir::Context &fctx,
                           foptim::fir::FunctionR ffunc,
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

inline void convert_switch(llvm::SwitchInst *switch_instr,
                           foptim::fir::Context &fctx,
                           foptim::fir::FunctionR ffunc,
                           foptim::fir::Builder &builder, V2VMap &valueToValue,
                           llvm::Module &mod, B2BMap &b2b) {
  (void)switch_instr;
  (void)fctx;
  (void)ffunc;
  (void)builder;
  (void)valueToValue;
  (void)mod;
  (void)b2b;

  auto defaultDest =
      foptim::fir::BasicBlock{foptim::fir::BasicBlock::invalid()};
  if (!switch_instr->defaultDestUndefined()) {
    defaultDest = b2b.at(switch_instr->getDefaultDest());
  }
  // auto *defaultDest = switch_instr->getDefaultDest();

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
        fctx->get_constant_value(
            val, fctx->get_int_type(case_llvm_value->getBitWidth())),
        target_bb);
  }

  auto value = convert_instr_arg(switch_instr->getCondition(), fctx, ffunc,
                                 builder, valueToValue, mod, b2b);

  builder.build_switch(value, cases, defaultDest);
}

inline void convert_call(const llvm::Instruction *any_instr,
                         const llvm::CallInst *call_instr,
                         foptim::fir::Context &fctx,
                         foptim::fir::FunctionR ffunc,
                         foptim::fir::Builder &builder, V2VMap &valueToValue,
                         llvm::Module &mod, B2BMap &b2b) {

  foptim::TVec<foptim::fir::ValueR> args = {};
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

inline bool convert_fcmp(const llvm::Instruction *any_instr,
                         const llvm::FCmpInst *cmp_inst,
                         foptim::fir::Context &fctx,
                         foptim::fir::FunctionR ffunc,
                         foptim::fir::Builder &builder, V2VMap &valueToValue,
                         llvm::Module &mod, B2BMap &b2b) {
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

inline bool convert_icmp(const llvm::Instruction *any_instr,
                         const llvm::ICmpInst *cmp_inst,
                         foptim::fir::Context &fctx,
                         foptim::fir::FunctionR ffunc,
                         foptim::fir::Builder &builder, V2VMap &valueToValue,
                         llvm::Module &mod, B2BMap &b2b) {

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

inline void convert(llvm::Instruction *any_instr, foptim::fir::Context &fctx,
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
    auto store = builder.build_store(ptr, value);
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
    auto load = builder.build_load(type, value);
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
  } else if (op_code == llvm::Instruction::Add) {
    auto left = convert_instr_arg(any_instr->getOperand(0), fctx, ffunc,
                                  builder, valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr->getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr->getType()->isIntegerTy()) {
      auto add = builder.build_int_add(left, right);
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
      auto add = builder.build_int_mul(left, right);
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
      args.emplace_back(
          fctx->get_constant_value(index, fctx->get_int_type(32)));
    }
    auto add = builder.build_extract_value(stru, args,
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
      args.emplace_back(
          fctx->get_constant_value(index, fctx->get_int_type(32)));
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

inline void generate_memset(foptim::fir::Context &fctx) {
  const auto *name = "foptim.memset";
  if (fctx->has_function(name)) {
    return;
  }
  auto func_ty = fctx->get_func_ty(
      fctx->get_void_type(),
      {fctx->get_ptr_type(), fctx->get_int_type(8), fctx->get_int_type(64)});
  auto ffunc = fctx->create_function(name, func_ty);
  ffunc.func->linkage = foptim::fir::Function::Linkage::LinkOnceODR;

  auto bb = ffunc.builder();
  auto entry_bb = ffunc->get_entry();
  auto loop_body = bb.append_bb();
  auto exit = bb.append_bb();

  // the arguments
  auto ptr_arg = foptim::fir::ValueR{entry_bb->args[0]};
  auto value_arg = foptim::fir::ValueR{entry_bb->args[1]};
  auto length_arg = foptim::fir::ValueR{entry_bb->args[2]};

  auto i64_ty = fctx->get_int_type(64);
  auto constant_zero = foptim::fir::ValueR(fctx->get_constant_value(0, i64_ty));
  auto constant_one = foptim::fir::ValueR(fctx->get_constant_value(1, i64_ty));
  auto constant_eight =
      foptim::fir::ValueR(fctx->get_constant_value(8, i64_ty));

  // === header
  bb.at_end(ffunc->get_entry());
  // i = 0
  auto index = bb.build_alloca(constant_eight);
  index.as_instr()->add_attrib("alloca::type", i64_ty);
  bb.build_store(index, constant_zero);
  // if(length != 0)
  auto loop_cond = bb.build_int_cmp(length_arg, constant_zero,
                                    foptim::fir::ICmpInstrSubType::NE);
  bb.build_cond_branch(loop_cond, loop_body, exit);

  // === loop
  {
    bb.at_end(loop_body);
    auto old_index_val = bb.build_load(i64_ty, index);
    // ptr+i = value
    auto target_offset = bb.build_int_add(ptr_arg, old_index_val);
    bb.build_store(target_offset, value_arg);

    // i++
    auto new_index_val = bb.build_int_add(old_index_val, constant_one);
    bb.build_store(index, new_index_val);
    // while(i+1 < length)
    auto loop_cond = bb.build_int_cmp(new_index_val, length_arg,
                                      foptim::fir::ICmpInstrSubType::ULT);
    bb.build_cond_branch(loop_cond, loop_body, exit);
  }

  // return
  bb.at_end(exit);
  bb.build_return();
}

inline void generate_fabs(foptim::fir::Context &fctx,
                          llvm::StringRef func_name) {

  const char *name = "invalidfabs";
  u32 width = 0;
  u64 constant_value = 0;
  if (func_name.ends_with("f64")) {
    name = "foptim.abs.f64";
    width = 64;
    constant_value = 0x7fffffffffffffff;
  } else if (func_name.ends_with("f32")) {
    name = "foptim.abs.f32";
    width = 32;
    constant_value = 0x7fffffff;
  } else {
    fmt::println("{}", func_name.str().c_str());
    TODO("IMPL");
  }
  if (fctx->has_function(name)) {
    return;
  }
  auto func_ty = fctx->get_func_ty(fctx->get_float_type(width),
                                   {fctx->get_float_type(width)});
  auto ffunc = fctx->create_function(name, func_ty);
  ffunc.func->linkage = foptim::fir::Function::Linkage::LinkOnceODR;

  auto bb = ffunc.builder();
  auto entry_bb = ffunc->get_entry();
  bb.at_end(entry_bb);

  auto constant = foptim::fir::ValueR(fctx->get_constant_value(
      std::bit_cast<foptim::f64>(constant_value), fctx->get_float_type(width)));
  auto res = bb.build_binary_op(foptim::fir::ValueR{entry_bb->args[0]},
                                constant, foptim::fir::BinaryInstrSubType::And);
  bb.build_return(res);
}

inline void generate_abs(foptim::fir::Context &fctx,
                         llvm::StringRef func_name) {
  const char *name = "invalidabs";
  u32 width = 0;
  if (func_name.ends_with("i64")) {
    name = "foptim.abs.i64";
    width = 64;
  } else if (func_name.ends_with("i32")) {
    name = "foptim.abs.i32";
    width = 32;
  } else {
    fmt::println("{}", func_name.str().c_str());
    TODO("IMPL");
  }

  if (fctx->has_function(name)) {
    return;
  }

  auto width_type = fctx->get_int_type(width);
  auto func_ty = fctx->get_func_ty(width_type, {width_type});
  auto ffunc = fctx->create_function(name, func_ty);
  ffunc.func->linkage = foptim::fir::Function::Linkage::Internal;

  auto bb = ffunc.builder();
  auto entry_bb = ffunc->get_entry();
  bb.at_end(entry_bb);

  auto constant_zero =
      foptim::fir::ValueR(fctx->get_constant_value((u64)0, width_type));
  auto arg = foptim::fir::ValueR{entry_bb->args[0]};
  auto cond =
      bb.build_int_cmp(arg, constant_zero, foptim::fir::ICmpInstrSubType::SGT);
  auto negated = bb.build_unary_op(arg, foptim::fir::UnaryInstrSubType::IntNeg);
  auto res = bb.build_select(width_type, cond, arg, negated);
  bb.build_return(res);
}

inline void generate_memcpy(foptim::fir::Context &fctx) {
  const auto *name = "foptim.memcpy";
  if (fctx->has_function(name)) {
    return;
  }
  auto func_ty = fctx->get_func_ty(
      fctx->get_void_type(),
      {fctx->get_ptr_type(), fctx->get_ptr_type(), fctx->get_int_type(64)});
  auto ffunc = fctx->create_function(name, func_ty);
  ffunc.func->linkage = foptim::fir::Function::Linkage::Internal;

  auto bb = ffunc.builder();
  auto entry_bb = ffunc->get_entry();
  auto loop_body = bb.append_bb();
  auto exit = bb.append_bb();

  // the arguments
  auto dst_ptr_arg = foptim::fir::ValueR{entry_bb->args[0]};
  auto src_ptr_arg = foptim::fir::ValueR{entry_bb->args[1]};
  auto length_arg = foptim::fir::ValueR{entry_bb->args[2]};

  auto i8_ty = fctx->get_int_type(8);
  auto i64_ty = fctx->get_int_type(64);
  auto constant_zero = foptim::fir::ValueR(fctx->get_constant_value(0, i64_ty));
  auto constant_one = foptim::fir::ValueR(fctx->get_constant_value(1, i64_ty));
  auto constant_eight =
      foptim::fir::ValueR(fctx->get_constant_value(8, i64_ty));

  // === header
  bb.at_end(ffunc->get_entry());
  // i = 0
  auto index = bb.build_alloca(constant_eight);
  index.as_instr()->add_attrib("alloca::type", i64_ty);
  bb.build_store(index, constant_zero);
  // if(length != 0)
  auto loop_cond = bb.build_int_cmp(length_arg, constant_zero,
                                    foptim::fir::ICmpInstrSubType::NE);
  bb.build_cond_branch(loop_cond, loop_body, exit);

  // === loop
  {
    bb.at_end(loop_body);
    auto old_index_val = bb.build_load(i64_ty, index);
    // ptr+i = value
    auto src_offset = bb.build_int_add(src_ptr_arg, old_index_val);
    auto val = bb.build_load(i8_ty, src_offset);
    auto dst_offset = bb.build_int_add(dst_ptr_arg, old_index_val);
    bb.build_store(dst_offset, val);

    // i++
    auto new_index_val = bb.build_int_add(old_index_val, constant_one);
    bb.build_store(index, new_index_val);

    // while(i+1 < length)
    auto loop_cond = bb.build_int_cmp(new_index_val, length_arg,
                                      foptim::fir::ICmpInstrSubType::ULT);
    bb.build_cond_branch(loop_cond, loop_body, exit);
  }

  // return
  bb.at_end(exit);
  bb.build_return();
}

inline void generate_trap(foptim::fir::Context &fctx) {
  if (fctx->has_function("abort")) {
    return;
  }
  auto func_ty = fctx->get_func_ty(fctx->get_void_type(), {});
  fctx.data->storage.functions.insert(
      {"abort", std::make_unique<foptim::fir::Function>(fctx.operator->(),
                                                        "abort", func_ty)});
}

inline void generate_memmove(foptim::fir::Context &fctx) {
  if (fctx->has_function("memmove")) {
    return;
  }
  auto func_ty = fctx->get_func_ty(
      fctx->get_ptr_type(),
      {fctx->get_ptr_type(), fctx->get_ptr_type(), fctx->get_int_type(64)});
  fctx.data->storage.functions.insert(
      {"memmove", std::make_unique<foptim::fir::Function>(fctx.operator->(),
                                                          "memmove", func_ty)});
}

inline void generate_fexp(foptim::fir::Context &fctx) {
  if (fctx->has_function("exp")) {
    return;
  }
  auto func_ty =
      fctx->get_func_ty(fctx->get_float_type(64), {fctx->get_float_type(64)});
  fctx.data->storage.functions.insert(
      {"exp", std::make_unique<foptim::fir::Function>(fctx.operator->(), "exp",
                                                      func_ty)});
}

inline void convert_decl(llvm::Function &func, foptim::fir::Context &fctx,
                         V2VMap &valueToValue, llvm::Module &mod) {
  if (func.getName().starts_with("llvm.memset")) {
    generate_memset(fctx);
  } else if (func.getName().starts_with("llvm.memcpy")) {
    generate_memcpy(fctx);
  } else if (func.getName().starts_with("llvm.trap")) {
    generate_trap(fctx);
  } else if (func.getName().starts_with("llvm.fabs")) {
    generate_fabs(fctx, func.getName());
  } else if (func.getName().starts_with("llvm.abs")) {
    generate_abs(fctx, func.getName());
  } else if (func.getName().starts_with("llvm.exp.f")) {
    generate_fexp(fctx);
  } else if (func.getName().starts_with("llvm.memmove")) {
    generate_memmove(fctx);
  }
  foptim::IRString func_name = func.getName().str().c_str();
  fctx.data->storage.functions.insert(
      {func_name, std::make_unique<foptim::fir::Function>(
                      fctx.operator->(), func_name,
                      convert_type(func.getFunctionType(), fctx, mod))});

  const auto foff_func = fctx->get_function(func_name.c_str());
  const auto func_ptr = fctx->get_constant_value(foff_func);
  valueToValue.insert({&func, foptim::fir::ValueR{func_ptr}});
}

inline void setup_function(llvm::Function &func, foptim::fir::Context &fctx,
                           V2VMap &valueToValue, llvm::Module &mod) {
  if (!func.hasName()) {
    return;
  }

  foptim::IRString func_name = func.getName().str().c_str();

  if (func.empty()) {
    convert_decl(func, fctx, valueToValue, mod);
  } else {
    auto foff_ftype = convert_type(func.getFunctionType(), fctx, mod);
    auto foff_func = fctx->create_function(func_name, foff_ftype);
    auto func_ptr = fctx->get_constant_value(foff_func);
    valueToValue.insert({&func, foptim::fir::ValueR{func_ptr}});
  }

  const auto foff_func = fctx->get_function(func.getName().str().c_str());
  if (func.isVarArg()) {
    foff_func->variadic = true;
  }

  switch (func.getCallingConv()) {
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
    foff_func->linkage = foptim::fir::Function::Linkage::Internal;
    break;
  case llvm::GlobalValue::LinkOnceAnyLinkage:
    foff_func->linkage = foptim::fir::Function::Linkage::LinkOnce;
    break;
  case llvm::GlobalValue::LinkOnceODRLinkage:
    foff_func->linkage = foptim::fir::Function::Linkage::LinkOnceODR;
    break;
  case llvm::GlobalValue::WeakAnyLinkage:
    foff_func->linkage = foptim::fir::Function::Linkage::Weak;
    break;
  case llvm::GlobalValue::WeakODRLinkage:
    foff_func->linkage = foptim::fir::Function::Linkage::WeakODR;
    break;
  case llvm::GlobalValue::ExternalLinkage:
  case llvm::GlobalValue::AvailableExternallyLinkage:
  case llvm::GlobalValue::AppendingLinkage:
  case llvm::GlobalValue::ExternalWeakLinkage:
  case llvm::GlobalValue::CommonLinkage:
    foff_func->linkage = foptim::fir::Function::Linkage::External;
    break;
  }
  if (foptim::utils::all_linkage_internal && func_name != "main" &&
      !func.empty()) {
    foff_func->linkage = foptim::fir::Function::Linkage::Internal;
  }

  if (func.mustProgress()) {
    foff_func->must_progress = true;
  }
  // readNone: 0, readOnly: 0, noInline: 0, alwaysInline:
  if (func.doesNotRecurse()) {
    foff_func->no_recurse = true;
  }
  if (func.doesNotAccessMemory()) {
    foff_func->mem_read_none = true;
  } else if (func.onlyReadsMemory()) {
    foff_func->mem_read_only = true;
  }
}

inline void convert(llvm::Function &func, foptim::fir::Context &fctx,
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

inline void
convert_constant_init(const uint8_t *output, const llvm::Constant *val,
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
      llvm::errs() << "constant int " << *d << " " << d->getBitWidth() << "\n";
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
      ASSERT(gep->getSourceElementType()->isIntegerTy() &&
             gep->getSourceElementType()->getIntegerBitWidth() == 8);

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

        llvm::errs() << (u64)val << "\n";
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
    // foptim::fir::ConstantValueR reloc_ref = valueToValue.at(d).as_constant();
    // glob->reloc_info.push_back({reloc_off, reloc_ref});
    return;
  }
  if (const auto *d = llvm::dyn_cast_or_null<llvm::Function>(val)) {
    size_t reloc_off = output - glob->init_value;
    llvm::errs() << "getting func" << valueToValue.contains(d) << " "
                 << d->getName() << "\n";
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
               << (llvm::dyn_cast_or_null<llvm::ConstantData>(val) != nullptr)
               << "\n";
  llvm::errs() << "isConstantExpr: "
               << (llvm::dyn_cast_or_null<llvm::ConstantExpr>(val) != nullptr)
               << "\n";
  llvm::errs() << "isGlobalValue: "
               << (llvm::dyn_cast_or_null<llvm::GlobalValue>(val) != nullptr)
               << "\n";
  llvm::errs() << "isConstantAggregate: "
               << (llvm::dyn_cast_or_null<llvm::ConstantAggregate>(val) !=
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

    auto global = fctx->get_global(name, actual_size);
    auto as_global = fctx->get_constant_value(global);
    global->is_constant = val->isConstant();
    global->init_value =
        foptim::utils::IRAlloc<uint8_t>{}.allocate(actual_size);
    memset(global->init_value, 0, actual_size);
    valueToValue.insert({(llvm::Value *)&gval, foptim::fir::ValueR(as_global)});
  }
}

inline void convert(llvm::Module &mod, llvm::GlobalValue &gval,
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

inline void convert(llvm::Module &mod, foptim::fir::Context &fctx) {
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
      convert(func, fctx, valueToValue);
    }
  }
}

void load_llvm_ir(const char *filename, foptim::fir::Context &fctx) {
  llvm::LLVMContext context;
  llvm::SMDiagnostic error;
  std::unique_ptr<llvm::Module> module;
  {
    ZoneScopedN("LLVM");
    module = llvm::parseIRFile(filename, error, context);
  }
  if (module) {
    // module->dump();
    convert(*module, fctx);
  } else {
    llvm::errs() << "FAILED TO LOAD: '" << filename << "' "
                 << error.getMessage() << "\n";
  }
}
