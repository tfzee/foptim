#pragma once
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/module_pass.hpp"

namespace foptim::optim {
static void constant_prop_args(fir::FunctionR func, fir::Context &ctx);

// inter procedural constant propagation
// replace arguments insideo of functions with constant args
class IPCP final : public ModulePass {
public:
  void apply(fir::Context &ctx) override {
    for (auto &f : ctx.data->storage.functions) {
      constant_prop_args(fir::FunctionR(&f.second), ctx);
    }
  }
};

static void constant_prop_args(fir::FunctionR func, fir::Context &ctx) {
  if (func.func->linkage != fir::Function::Linkage::Internal) {
    return;
  }
  if (func->is_decl()) {
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
    }
  }
  fmt::println("===== {}", arg_tys.size());
  for (auto t : arg_tys) {
    fmt::println("{}", t);
  }
  func.func->func_ty =
      ctx->get_func_ty(func_ty.return_type, std::move(arg_tys));
}

} // namespace foptim::optim
