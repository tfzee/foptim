#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/function.hpp"
#include "ir/function_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <cstdlib>
#include <deque>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <unordered_set>

using foptim::u32;
using foptim::u64;

typedef foptim::FMap<const llvm::Value *, foptim::fir::ValueR> V2VMap;

inline void
convert(llvm::Instruction &any_instr, foptim::fir::Context &fctx,
        foptim::fir::FunctionR ffunc, foptim::fir::Builder &builder,
        V2VMap &valueToValue, llvm::Module &mod,
        foptim::FMap<llvm::BasicBlock *, foptim::fir::BasicBlock> &b2b);

inline foptim::fir::ValueR convert_instr_arg(
    const llvm::Value *value, foptim::fir::Context &fctx,
    foptim::fir::FunctionR ffunc, foptim::fir::Builder &builder,
    V2VMap &valueToValue, llvm::Module &mod,
    foptim::FMap<llvm::BasicBlock *, foptim::fir::BasicBlock> &b2b) {
  (void)ffunc;
  (void)builder;
  if (const auto *const_value = dyn_cast_or_null<llvm::ConstantExpr>(value)) {
    auto *instr = const_value->getAsInstruction();
    ASSERT(instr != nullptr);
    convert(*instr, fctx, ffunc, builder, valueToValue, mod, b2b);
    return valueToValue.at(instr);
  }
  if (valueToValue.contains(value)) {
    return valueToValue.at(value);
  }

  if (const auto *int_constant =
          llvm::dyn_cast_or_null<llvm::ConstantInt>(value)) {

    u64 val = int_constant->getZExtValue();
    u32 bitwidth = int_constant->getBitWidth();

    return foptim::fir::ValueR(
        fctx->get_constant_value(val, fctx->get_int_type(bitwidth)));
  }

  llvm::errs() << value << " " << typeid(value).name() << "\n";
  llvm::errs() << *value << "\n";

  std::abort();
}

inline foptim::fir::TypeR convert_type(llvm::Type *any_ty,
                                       foptim::fir::Context &ctx) {
  if (auto *v = llvm::dyn_cast_or_null<llvm::IntegerType>(any_ty)) {
    u32 width = v->getBitWidth();
    return ctx->get_int_type(width);
  }
  if (auto *v = llvm::dyn_cast_or_null<llvm::PointerType>(any_ty)) {

    return ctx->get_ptr_type();
  }
  if (any_ty->isVoidTy()) {
    return ctx->get_void_type();
  }
  if (auto *v = llvm::dyn_cast_or_null<llvm::FunctionType>(any_ty)) {
    auto ret_type = convert_type(v->getReturnType(), ctx);

    foptim::FVec<foptim::fir::TypeR> args;
    args.reserve(v->getNumParams());
    for (size_t i = 0; i < v->getNumParams(); i++) {
      auto *param = v->getParamType(i);
      args.push_back(convert_type(param, ctx));
    }
    return ctx->get_func_ty(ret_type, args);
  }
  if (any_ty->isStructTy()) {
  }

  llvm::errs() << "FAILED TO CONVERT LLVM IR TYPE TO NORMAL TYPE TODO\n";
  llvm::errs() << *any_ty << "\n" << "TODO\n";
  std::abort();
}

inline void convert_alloca(const llvm::Instruction &any_instr,
                           const llvm::AllocaInst *alloca_instr,
                           foptim::fir::Context &fctx,
                           foptim::fir::Builder &builder, V2VMap &valueToValue,
                           llvm::Module &mod) {

  // auto mod = instr->getModule();
  auto datalayout = mod.getDataLayout();

  auto *llvm_type = alloca_instr->getAllocatedType();
  auto type_size = datalayout.getTypeAllocSize(llvm_type).getFixedValue();

  auto alloca = builder.build_alloca(foptim::fir::ValueR(
      fctx->get_constant_value(type_size, fctx->get_int_type(32))));

  if (!llvm_type->isAggregateType()) {
    auto type = convert_type(llvm_type, fctx);
    alloca.as_instr()->add_attrib("alloca::type", type);
  }
  valueToValue.insert({&any_instr, alloca});
}

inline void
convert_gep(const llvm::Instruction &any_instr,
            const llvm::GetElementPtrInst *gep_instr,
            foptim::fir::Context &fctx, foptim::fir::FunctionR ffunc,
            foptim::fir::Builder &builder, V2VMap &valueToValue,
            llvm::Module &mod,
            foptim::FMap<llvm::BasicBlock *, foptim::fir::BasicBlock> &b2b) {

  auto ptr = convert_instr_arg(gep_instr->getPointerOperand(), fctx, ffunc,
                               builder, valueToValue, mod, b2b);
  foptim::FVec<llvm::Value *> args = {};
  auto datalayout = mod.getDataLayout();
  auto result_value = ptr;

  for (const auto &indx : gep_instr->indices()) {
    auto arg_foptim = convert_instr_arg(indx.get(), fctx, ffunc, builder,
                                        valueToValue, mod, b2b);
    args.push_back(indx.get());

    u64 arg_mul = datalayout
                      .getTypeAllocSize(llvm::GetElementPtrInst::getIndexedType(
                          gep_instr->getSourceElementType(), args))
                      .getFixedValue();
    auto arg_mul_value =
        fctx->get_constant_value(arg_mul, fctx->get_int_type(32));

    auto mul =
        builder.build_int_mul(arg_foptim, foptim::fir::ValueR(arg_mul_value));
    result_value = builder.build_int_add(result_value, mul);
  }
  valueToValue.insert({&any_instr, result_value});

  // FVec<foptim::fir::ValueR> args = {};
  // instr->getTypeAtIndex();
  // instr->getIndexedType()
  // for (auto &indx : instr->indices()) {
  //   auto arg_foptim =
  //       convert_instr_arg(indx.get(), fctx, ffunc, builder, valueToValue);
  //   args.push_back(arg_foptim);
  // }

  // auto instr_type = convert_type(instr->getType(), fctx);

  // auto ptr = convert_instr_arg(instr->getPointerOperand(), fctx, ffunc,
  // builder, valueToValue); auto res = builder.build_gep(ptr, instr_type,
  // args); valueToValue.insert({&any_instr, res});
}

inline void
convert_branch(const llvm::BranchInst *branch_instr, foptim::fir::Context &fctx,
               foptim::fir::FunctionR ffunc, foptim::fir::Builder &builder,
               V2VMap &valueToValue, llvm::Module &mod,
               foptim::FMap<llvm::BasicBlock *, foptim::fir::BasicBlock> &b2b) {

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

inline void
convert_call(const llvm::Instruction &any_instr,
             const llvm::CallInst *call_instr, foptim::fir::Context &fctx,
             foptim::fir::FunctionR ffunc, foptim::fir::Builder &builder,
             V2VMap &valueToValue, llvm::Module &mod,
             foptim::FMap<llvm::BasicBlock *, foptim::fir::BasicBlock> &b2b) {

  foptim::FVec<foptim::fir::ValueR> args = {};
  for (size_t i = 0; i < call_instr->getNumOperands() - 1; i++) {
    auto *arg = call_instr->getOperand(i);
    auto arg_foptim =
        convert_instr_arg(arg, fctx, ffunc, builder, valueToValue, mod, b2b);
    args.push_back(arg_foptim);
  }

  auto *ret_type = call_instr->getFunctionType()->getReturnType();
  auto func_type_foptim = convert_type(call_instr->getFunctionType(), fctx);
  auto ret_type_foptim = convert_type(ret_type, fctx);
  auto res = builder.build_direct_call(
      call_instr->getCalledFunction()->getName().str(), func_type_foptim,
      ret_type_foptim, args);
  valueToValue.insert({&any_instr, res});
}

inline bool
convert_icmp(const llvm::Instruction &any_instr, const llvm::ICmpInst *cmp_inst,
             foptim::fir::Context &fctx, foptim::fir::FunctionR ffunc,
             foptim::fir::Builder &builder, V2VMap &valueToValue,
             llvm::Module &mod,
             foptim::FMap<llvm::BasicBlock *, foptim::fir::BasicBlock> &b2b) {

  switch (cmp_inst->getPredicate()) {
  case llvm::CmpInst::ICMP_EQ:
  case llvm::CmpInst::ICMP_NE:
  case llvm::CmpInst::ICMP_UGT:
  case llvm::CmpInst::ICMP_UGE:
  case llvm::CmpInst::ICMP_ULE:
  case llvm::CmpInst::ICMP_SGT:
  case llvm::CmpInst::ICMP_SGE:
  case llvm::CmpInst::ICMP_SLE:
  case llvm::CmpInst::ICMP_ULT:
  case llvm::CmpInst::ICMP_SLT: {
    auto a = convert_instr_arg(cmp_inst->getOperand(0), fctx, ffunc, builder,
                               valueToValue, mod, b2b);
    auto b = convert_instr_arg(cmp_inst->getOperand(1), fctx, ffunc, builder,
                               valueToValue, mod, b2b);
    foptim::fir::ICmpInstrSubType pred;
    if (cmp_inst->getPredicate() == llvm::CmpInst::ICMP_ULT) {
      pred = foptim::fir::ICmpInstrSubType::ULT;
    } else if (cmp_inst->getPredicate() == llvm::CmpInst::ICMP_SLT) {
      pred = foptim::fir::ICmpInstrSubType::SLT;
    } else {
      return false;
    }
    auto res = builder.build_int_cmp(a, b, pred);
    valueToValue.insert({&any_instr, res});
    return true;
  }
  case llvm::CmpInst::FCMP_FALSE:
  case llvm::CmpInst::FCMP_OEQ:
  case llvm::CmpInst::FCMP_OGT:
  case llvm::CmpInst::FCMP_OGE:
  case llvm::CmpInst::FCMP_OLT:
  case llvm::CmpInst::FCMP_OLE:
  case llvm::CmpInst::FCMP_ONE:
  case llvm::CmpInst::FCMP_ORD:
  case llvm::CmpInst::FCMP_UNO:
  case llvm::CmpInst::FCMP_UEQ:
  case llvm::CmpInst::FCMP_UGT:
  case llvm::CmpInst::FCMP_UGE:
  case llvm::CmpInst::FCMP_ULT:
  case llvm::CmpInst::FCMP_ULE:
  case llvm::CmpInst::FCMP_UNE:
  case llvm::CmpInst::FCMP_TRUE:
  case llvm::CmpInst::BAD_FCMP_PREDICATE:
  case llvm::CmpInst::BAD_ICMP_PREDICATE:
    assert(false);
  }
}

inline void
convert(llvm::Instruction &any_instr, foptim::fir::Context &fctx,
        foptim::fir::FunctionR ffunc, foptim::fir::Builder &builder,
        V2VMap &valueToValue, llvm::Module &mod,
        foptim::FMap<llvm::BasicBlock *, foptim::fir::BasicBlock> &b2b) {

  ZoneScopedN("Convert Instr");
  if (const auto *instr =
          llvm::dyn_cast_or_null<llvm::ReturnInst>(&any_instr)) {
    if (auto *v = instr->getReturnValue()) {
      auto fv =
          convert_instr_arg(v, fctx, ffunc, builder, valueToValue, mod, b2b);
      builder.build_return(fv);
    } else {
      builder.build_return();
    }
    return;
  }
  if (const auto *instr = llvm::dyn_cast_or_null<llvm::StoreInst>(&any_instr)) {
    assert(instr->getNumOperands() == 2);
    auto value = convert_instr_arg(instr->getOperand(0), fctx, ffunc, builder,
                                   valueToValue, mod, b2b);
    auto ptr = convert_instr_arg(instr->getOperand(1), fctx, ffunc, builder,
                                 valueToValue, mod, b2b);
    auto store = builder.build_store(ptr, value);
    valueToValue.insert({&any_instr, store});
    return;
  }
  if (const auto *instr = llvm::dyn_cast_or_null<llvm::LoadInst>(&any_instr)) {
    assert(instr->getNumOperands() == 1);
    auto value = convert_instr_arg(instr->getOperand(0), fctx, ffunc, builder,
                                   valueToValue, mod, b2b);
    auto type = convert_type(instr->getAccessType(), fctx);
    auto load = builder.build_load(type, value);
    valueToValue.insert({&any_instr, load});
    return;
  }
  if (const auto *instr =
          llvm::dyn_cast_or_null<llvm::AllocaInst>(&any_instr)) {
    convert_alloca(any_instr, instr, fctx, builder, valueToValue, mod);
    return;
  }
  if (const auto *instr =
          llvm::dyn_cast_or_null<llvm::BranchInst>(&any_instr)) {
    convert_branch(instr, fctx, ffunc, builder, valueToValue, mod, b2b);
    return;
  }
  if (const auto *instr = llvm::dyn_cast_or_null<llvm::CallInst>(&any_instr)) {
    convert_call(any_instr, instr, fctx, ffunc, builder, valueToValue, mod,
                 b2b);
    return;
  }
  if (const auto *instr =
          llvm::dyn_cast_or_null<llvm::GetElementPtrInst>(&any_instr)) {
    convert_gep(any_instr, instr, fctx, ffunc, builder, valueToValue, mod, b2b);
    return;
  }
  if (const auto *instr = dyn_cast<llvm::SExtInst>(&any_instr)) {
    auto arg = convert_instr_arg(instr->getOperand(0), fctx, ffunc, builder,
                                 valueToValue, mod, b2b);
    auto dest_ty = convert_type(instr->getDestTy(), fctx);
    auto add = builder.build_sext(arg, dest_ty);
    valueToValue.insert({&any_instr, add});
    return;
  }
  if (const auto *instr = llvm::dyn_cast_or_null<llvm::ICmpInst>(&any_instr)) {
    if (convert_icmp(any_instr, instr, fctx, ffunc, builder, valueToValue, mod,
                     b2b)) {
      return;
    }
  } else if (any_instr.getOpcode() == llvm::Instruction::Add) {
    auto left = convert_instr_arg(any_instr.getOperand(0), fctx, ffunc, builder,
                                  valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr.getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr.getType()->isIntegerTy()) {
      auto add = builder.build_int_add(left, right);
      valueToValue.insert({&any_instr, add});
      return;
    }
  } else if (any_instr.getOpcode() == llvm::Instruction::Mul) {
    auto left = convert_instr_arg(any_instr.getOperand(0), fctx, ffunc, builder,
                                  valueToValue, mod, b2b);
    auto right = convert_instr_arg(any_instr.getOperand(1), fctx, ffunc,
                                   builder, valueToValue, mod, b2b);

    if (any_instr.getType()->isIntegerTy()) {
      auto add = builder.build_int_mul(left, right);
      valueToValue.insert({&any_instr, add});
      return;
    }
  }
  llvm::errs() << any_instr << "\n" << "TODO\n";
  TODO("");
  std::abort();
}

inline void convert(llvm::Function &func, foptim::fir::Context &fctx,
                    V2VMap &valueToValue) {
  ZoneScopedN("Convert Func");
  if (func.empty()) {
    return;
  }

  auto name = func.getName();
  auto ffunc = fctx->create_function(
      name.str(), convert_type(func.getFunctionType(), fctx));

  switch (func.getLinkage()) {
  case llvm::GlobalValue::InternalLinkage:
  case llvm::GlobalValue::PrivateLinkage:
    ffunc->linkage = foptim::fir::Function::Linkage::Internal;
  case llvm::GlobalValue::ExternalLinkage:
  case llvm::GlobalValue::AvailableExternallyLinkage:
  case llvm::GlobalValue::LinkOnceAnyLinkage:
  case llvm::GlobalValue::LinkOnceODRLinkage:
  case llvm::GlobalValue::WeakAnyLinkage:
  case llvm::GlobalValue::WeakODRLinkage:
  case llvm::GlobalValue::AppendingLinkage:
  case llvm::GlobalValue::ExternalWeakLinkage:
  case llvm::GlobalValue::CommonLinkage:
    ffunc->linkage = foptim::fir::Function::Linkage::External;
    break;
  }

  switch (func.getCallingConv()) {
  case llvm::CallingConv::C:
    ffunc->cc = foptim::fir::Function::CallingConv::C;
    break;
  default:
    foptim::utils::Debug << "Not supporting calling convention:"
                         << func.getCallingConv() << "\n";
    TODO("");
  }
  // func.getMemoryEffects()
  // func.isConvergent()

  auto fbuilder = ffunc.builder();

  llvm::BasicBlock &entry_bb = func.getEntryBlock();

  std::unordered_set<llvm::BasicBlock *> visited_bbs;
  std::deque<std::pair<llvm::BasicBlock *, foptim::fir::BasicBlock>> worklist{
      {&entry_bb, ffunc->get_entry_bb()}};

  foptim::FMap<llvm::BasicBlock *, foptim::fir::BasicBlock> b2b{};
  auto fentry_bb = ffunc->get_entry_bb();
  b2b.insert({&entry_bb, fentry_bb});

  auto &fargs = fentry_bb->get_args();
  for (size_t arg_idx = 0; arg_idx < fargs.size(); arg_idx++) {
    // valueToValue[func.getArg(arg_idx)] = fargs[arg_idx];
    valueToValue.insert(
        {func.getArg(arg_idx), foptim::fir::ValueR(fentry_bb, arg_idx)});
  }

  while (!worklist.empty()) {
    auto [bb_llvm, bb_foptim] = worklist.front();
    worklist.pop_front();
    fbuilder.at_end(bb_foptim);

    visited_bbs.insert(bb_llvm);

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
          }
        }
      }
      convert(instr, fctx, ffunc, fbuilder, valueToValue, *func.getParent(),
              b2b);
    }
  }
}

inline void convert(llvm::Module &mod, llvm::GlobalValue &gval,
                    foptim::fir::Context &fctx, V2VMap &valueToValue) {
  if (const auto *val = dyn_cast_or_null<llvm::GlobalVariable>(&gval)) {
    // auto type = convert_type(val->getValueType(), fctx);
    if (val->hasInitializer()) {
      foptim::utils::Debug << "TODO: handle global init\n";
    }

    auto data_layout = mod.getDataLayout();

    auto global_size = data_layout.getTypeAllocSize(gval.getValueType());
    ASSERT(!global_size.isScalable());
    auto global = fctx->get_global(global_size.getFixedValue());
    auto as_global = fctx->get_constant_value(global);
    valueToValue.insert({(llvm::Value *)&gval, foptim::fir::ValueR(as_global)});
  } else {
    llvm::errs() << "Not handling global " << gval;
  }
}

inline void convert(llvm::Module &mod, foptim::fir::Context &fctx) {
  V2VMap valueToValue;
  for (auto &globals : mod.global_values()) {
    convert(mod, globals, fctx, valueToValue);
  }
  for (auto &func : mod.functions()) {
    convert(func, fctx, valueToValue);
  }
}

void load_llvm_ir(const char *filename, foptim::fir::Context &fctx) {
  (void)filename;
  (void)fctx;

  llvm::LLVMContext context;
  llvm::SMDiagnostic error;
  auto module = llvm::parseIRFile(filename, error, context);

  if (module) {
    convert(*module, fctx);
    module->dump();
  } else {
    llvm::errs() << "FAILED TO LOAD" << error.getMessage() << "\n";
  }
}
