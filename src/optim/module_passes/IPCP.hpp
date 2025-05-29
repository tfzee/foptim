#pragma once
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/module_pass.hpp"
#include <utility>

namespace foptim::optim {
static void constant_prop_args(fir::FunctionR func, fir::Context &ctx);

// inter procedural constant propagation
// replace arguments insideo of functions with constant args
class IPCP final : public ModulePass {
public:
  void apply(fir::Context &ctx) override {
    ZoneScopedN("IPCP");
    for (auto &f : ctx.data->storage.functions) {
      constant_prop_args(fir::FunctionR(f.second.get()), ctx);
    }
  }
};

static void constant_prop_args(fir::FunctionR func, fir::Context &ctx) {
  switch (func.func->linkage) {
  case fir::Function::Linkage::External:
  case fir::Function::Linkage::Weak:
  case fir::Function::Linkage::LinkOnce:
  case fir::Function::Linkage::WeakODR:
  case fir::Function::Linkage::LinkOnceODR:
    //TODO: check why this fails and fix it
    return;
  case fir::Function::Linkage::Internal:
    break;
  }

  if (func->is_decl() || func->variadic) {
    return;
  }

  // constant propagate arguments
  auto entry_block = func->get_entry();
  auto func_ty = func.func->func_ty->as_func();
  auto arg_tys = func_ty.arg_types;
  auto n_args_original = arg_tys.size();
  if (n_args_original == 0) {
    return;
  }

  for (auto use : func.func->get_uses()) {
    if (!use.user->is(fir::InstrType::CallInstr) ||
        use.type != fir::UseType::NormalArg || use.argId != 0) {
      return;
    }
  }

  bool modified = false;
  for (u64 i = n_args_original - 1; i > 0; i--) {
    bool can_convert = true;
    fir::ConstantValueR consti =
        fir::ConstantValueR(fir::ConstantValueR::invalid());

    for (auto use : func.func->get_uses()) {
      if (!use.user->args[i].is_constant()) {
        can_convert = false;
        break;
      }
      auto new_const = use.user->args[i].as_constant();
      if (consti.is_valid() && !consti->eql(*new_const.get_raw_ptr())) {
        can_convert = false;
        break;
      }
      consti = new_const;
    }

    if (can_convert && func.func->get_n_uses() != 0) {
      entry_block->args[i - 1]->replace_all_uses(fir::ValueR(consti));
      entry_block->remove_arg(i - 1);
      for (auto use : func.func->get_uses()) {
        use.user.remove_arg(i, true);
        // use.user->args.erase(use.user->args.begin() + i);
      }
      arg_tys.erase(arg_tys.begin() + (i - 1));
      modified = true;
    }
  }
  if (modified) {
    func.func->func_ty =
        ctx->get_func_ty(func_ty.return_type, std::move(arg_tys));

    // we need renaming
    if (func->linkage == fir::Function::Linkage::LinkOnceODR) {
      auto old_name = func->name;
      auto new_name = old_name + "MODIPCP";
      auto func_moved = std::move(ctx->storage.functions.at(old_name));
      ctx->storage.functions.erase(old_name);
      func_moved->name = new_name;
      ctx->storage.functions.insert({new_name, std::move(func_moved)});
    }
  }
}

} // namespace foptim::optim
