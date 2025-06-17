#pragma once
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/module_pass.hpp"
#include <utility>

namespace foptim::optim {
static void constant_prop_args(fir::FunctionR func, fir::Context &ctx);
static void constant_prop_return(fir::FunctionR func, fir::Context &ctx);
static void kill_dead_args(fir::FunctionR func, fir::Context &ctx);

// inter procedural constant propagation
// replace arguments insideo of functions with constant args
class IPCP final : public ModulePass {
public:
  void apply(fir::Context &ctx) override {
    ZoneScopedN("IPCP");
    for (auto &f : ctx.data->storage.functions) {
      switch (f.second->linkage) {
      case fir::Linkage::External:
      case fir::Linkage::Weak:
      case fir::Linkage::LinkOnce:
      case fir::Linkage::WeakODR:
      case fir::Linkage::LinkOnceODR:
        // TODO: could do with linkonce when enabling the renaming at the bottom
        continue;
      case fir::Linkage::Internal:
        break;
      }

      if (f.second->is_decl() || f.second->variadic) {
        continue;
      }
      for (auto use : f.second->get_uses()) {
        if (!use.user->is(fir::InstrType::CallInstr) ||
            use.type != fir::UseType::NormalArg || use.argId != 0) {
          continue;
        }
      }
      kill_dead_args(f.second.get(), ctx);
      constant_prop_args(fir::FunctionR(f.second.get()), ctx);
      constant_prop_return(fir::FunctionR(f.second.get()), ctx);
    }
  }
};
static void constant_prop_return(fir::FunctionR func, fir::Context & /*ctx*/) {
  auto ret_val = fir::ValueR();
  if (func->func_ty->as_func().return_type->is_void()) {
    return;
  }
  for (auto bb : func->basic_blocks) {
    for (auto instr : bb->instructions) {
      if (!instr->is(fir::InstrType::ReturnInstr)) {
        continue;
      }
      ASSERT(!instr->args.empty());
      if (ret_val.is_invalid()) {
        ret_val = instr->args[0];
        if (!ret_val.is_constant() && !ret_val.is_bb_arg()) {
          return;
        }
      } else if (ret_val != instr->args[0]) {
        return;
      }
    }
  }
  if (ret_val.is_constant()) {
    for (auto use : func->get_uses()) {
      use.user->replace_all_uses(ret_val);
    }
  }
  if ((ret_val.is_bb_arg() &&
       ret_val.as_bb_arg()->get_parent() == func->get_entry())) {
    auto id =
        ret_val.as_bb_arg()->get_parent()->get_arg_id(ret_val.as_bb_arg());

    for (auto use : func->get_uses()) {
      use.user->replace_all_uses(use.user->args[id+1]);
    }
  }
}

static void constant_prop_args(fir::FunctionR func, fir::Context &ctx) {
  switch (func.func->linkage) {
  case fir::Linkage::External:
  case fir::Linkage::Weak:
  case fir::Linkage::LinkOnce:
  case fir::Linkage::WeakODR:
  case fir::Linkage::LinkOnceODR:
    // TODO: check why this fails and fix it
    return;
  case fir::Linkage::Internal:
    break;
  }

  // constant propagate arguments
  auto entry_block = func->get_entry();
  auto func_ty = func.func->func_ty->as_func();
  auto n_args_original = func_ty.arg_types.size();
  if (n_args_original == 0) {
    return;
  }

  // TODO: this allocation is mostly gonna just waste memory
  auto arg_tys = func_ty.arg_types;

  bool modified = false;
  for (u64 i = n_args_original; i > 0; i--) {
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
    for (auto use : func.func->get_uses()) {
      use.user->set_attrib("callee_type", func.func->func_ty);
    }
    // we need renaming
    if (func->linkage == fir::Linkage::LinkOnceODR) {
      auto old_name = func->name;
      auto new_name = old_name + "MODIPCP";
      auto func_moved = std::move(ctx->storage.functions.at(old_name));
      ctx->storage.functions.erase(old_name);
      func_moved->name = new_name;
      ctx->storage.functions.insert({new_name, std::move(func_moved)});
    }
  }
}

static void kill_dead_args(fir::FunctionR func, fir::Context &ctx) {

  // constant propagate arguments
  auto entry_block = func->get_entry();
  auto func_ty = func.func->func_ty->as_func();
  auto n_args_original = func_ty.arg_types.size();
  if (n_args_original == 0) {
    return;
  }

  // TODO: this allocation is mostly gonna just waste memory
  auto arg_tys = func_ty.arg_types;

  bool modified = false;
  for (u64 i = n_args_original; i > 0; i--) {
    bool can_delete = entry_block->args[i - 1]->get_n_uses() == 0;

    if (can_delete) {
      entry_block->remove_arg(i - 1);
      for (auto use : func.func->get_uses()) {
        use.user.remove_arg(i, true);
      }
      arg_tys.erase(arg_tys.begin() + (i - 1));
      modified = true;
    }
  }
  if (modified) {
    func.func->func_ty =
        ctx->get_func_ty(func_ty.return_type, std::move(arg_tys));
    for (auto use : func.func->get_uses()) {
      use.user->set_attrib("callee_type", func.func->func_ty);
    }

    // // we need renaming
    // if (func->linkage == fir::Linkage::LinkOnceODR) {
    //   auto old_name = func->name;
    //   auto new_name = old_name + "MODIPCP";
    //   auto func_moved = std::move(ctx->storage.functions.at(old_name));
    //   ctx->storage.functions.erase(old_name);
    //   func_moved->name = new_name;
    //   ctx->storage.functions.insert({new_name, std::move(func_moved)});
    // }
  }
}
} // namespace foptim::optim
