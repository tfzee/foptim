#include "IPCP.hpp"

#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"

namespace foptim::optim {
namespace {

u64 ipcp_unique_name_number = 0;

void constant_prop_return(fir::FunctionR func, fir::Context &ctx) {
  TVec<fir::ValueR> ret_vals;
  if (func->func_ty->as_func().return_type->is_void()) {
    return;
  }
  for (auto bb : func->basic_blocks) {
    for (auto instr : bb->instructions) {
      if (!instr->is(fir::InstrType::ReturnInstr)) {
        continue;
      }
      ASSERT(!instr->args.empty());
      if (ret_vals.empty()) {
        for (auto ret_val : instr->args) {
          if (!ret_val.is_constant() && !ret_val.is_bb_arg()) {
            return;
          }
          ret_vals.push_back(ret_val);
        }
      } else {
        if (instr->args.size() != ret_vals.size()) {
          return;
        }
        for (size_t i = 0; i < ret_vals.size(); i++) {
          if (ret_vals[i] != instr->args[i]) {
            return;
          }
        }
      }
    }
  }

  if (ret_vals.size() == 1) {
    for (auto use : func->get_uses()) {
      if (ret_vals[0].is_constant()) {
        use.user->replace_all_uses(ret_vals[0]);
      } else if ((ret_vals[0].is_bb_arg() &&
                  ret_vals[0].as_bb_arg()->get_parent() == func->get_entry())) {
        auto id = ret_vals[0].as_bb_arg()->get_parent()->get_arg_id(
            ret_vals[0].as_bb_arg());
        use.user->replace_all_uses(use.user->args[id + 1]);
      } else {
        TODO("unreach?");
      }
    }
  } else {
    auto res_ty = func->func_ty->as_func().return_type;
    for (auto use : func->get_uses()) {
      fir::Builder buh(use.user);
      auto res_val = fir::ValueR{ctx->get_poisson_value(res_ty)};
      auto i = 0;
      for (auto ret_val : ret_vals) {
        fir::ValueR indicies[1] = {fir::ValueR{ctx->get_constant_int(i, 32)}};
        i++;
        if (ret_val.is_constant()) {
          res_val = buh.build_insert_value(res_val, ret_val, indicies, res_ty);
        } else if ((ret_val.is_bb_arg() &&
                    ret_val.as_bb_arg()->get_parent() == func->get_entry())) {
          auto id = ret_val.as_bb_arg()->get_parent()->get_arg_id(
              ret_val.as_bb_arg());
          res_val = buh.build_insert_value(res_val, use.user->args[id + 1],
                                           indicies, res_ty);
        } else {
          TODO("unreach?");
        }
      }
      use.user->replace_all_uses(res_val);
    }
  }
}

bool constant_prop_args(fir::FunctionR func, fir::Context &ctx) {
  // constant propagate arguments
  auto entry_block = func->get_entry();
  auto func_ty = func.func->func_ty->as_func();
  auto n_args_original = func_ty.arg_types.size();
  if (n_args_original == 0) {
    return false;
  }
  if (func.func->get_n_uses() == 0) {
    return false;
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

    if (can_convert) {
      // fmt::println("IPCP {}", func.func->name);
      // for (auto use : func.func->get_uses()) {
      //   fmt::println("{}", use.user);
      // }
      // fmt::println("{:cd}", consti);
      // fmt::println("=======CONV {}============", i);
      // fmt::println("{:cd}", *func.func);
      entry_block->args[i - 1]->replace_all_uses(fir::ValueR(consti));
      entry_block->remove_arg(i - 1);
      for (auto use : func.func->get_uses()) {
        use.user.remove_arg(i, true);
        // use.user->args.erase(use.user->args.begin() + i);
      }
      arg_tys.erase(arg_tys.begin() + (i - 1));
      // fmt::println("{:cd}", *func.func);
      modified = true;
    }
  }
  if (modified) {
    func.func->func_ty =
        ctx->get_func_ty(func_ty.return_type, std::move(arg_tys));
    for (auto use : func.func->get_uses()) {
      use.user->extra_type = func.func->func_ty;
    }
    // we need renaming
    if (func->linkage == fir::Linkage::LinkOnceODR ||
        func->getName().starts_with("foptim.")) {
      auto old_name = func->name;
      ipcp_unique_name_number++;
      auto new_name = old_name + "MODIPCP";
      new_name += std::to_string(ipcp_unique_name_number);
      ASSERT(!ctx->storage.functions.contains(new_name));
      auto func_moved = std::move(ctx->storage.functions.at(old_name));
      ctx->storage.functions.erase(old_name);
      func_moved->name = new_name;
      func_moved->linkage = fir::Linkage::Internal;
      func_moved->no_inline = false;
      ctx->storage.functions.insert({new_name, std::move(func_moved)});
      return true;
    }
  }
  return false;
}

bool kill_dead_args(fir::FunctionR func, fir::Context &ctx) {
  // constant propagate arguments
  auto entry_block = func->get_entry();
  auto func_ty = func.func->func_ty->as_func();
  auto n_args_original = func_ty.arg_types.size();
  if (n_args_original == 0) {
    return false;
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
      use.user->extra_type = func.func->func_ty;
    }

    if (func->linkage == fir::Linkage::LinkOnceODR ||
        func->getName().starts_with("foptim.")) {
      auto old_name = func->name;
      ipcp_unique_name_number++;
      auto new_name = old_name + "MODIPCP";
      new_name += std::to_string(ipcp_unique_name_number);
      ASSERT(!ctx->storage.functions.contains(new_name));
      auto func_moved = std::move(ctx->storage.functions.at(old_name));
      ctx->storage.functions.erase(old_name);
      func_moved->name = new_name;
      func_moved->linkage = fir::Linkage::Internal;
      // TODO: Technically legal but might be counter productive
      func_moved->no_inline = false;
      ctx->storage.functions.insert({new_name, std::move(func_moved)});
      return true;
    }
  }
  return false;
}
}  // namespace

void IPCP::apply(fir::Context &ctx, JobSheduler * /*unused*/) {
  ZoneScopedN("IPCP");
  for (auto &f : ctx.data->storage.functions) {
    switch (f.second->linkage) {
      case fir::Linkage::External:
      case fir::Linkage::Weak:
      case fir::Linkage::LinkOnce:
      case fir::Linkage::WeakODR:
        continue;
      case fir::Linkage::LinkOnceODR:
      case fir::Linkage::Internal:
        break;
    }

    if (f.second->is_decl() || f.second->variadic) {
      continue;
    }
    bool skip = false;
    for (auto use : f.second->get_uses()) {
      if (!use.user->is(fir::InstrType::CallInstr) ||
          use.type != fir::UseType::NormalArg || use.argId != 0) {
        skip = true;
        break;
      }
    }
    if (skip) {
      continue;
    }

    // if (constant_prop_args(fir::FunctionR(f.second.get()), ctx)) {
    //   continue;
    // }
    if (kill_dead_args(f.second.get(), ctx)) {
      continue;
    }
    if (constant_prop_args(fir::FunctionR(f.second.get()), ctx)) {
      continue;
    }
    constant_prop_return(fir::FunctionR(f.second.get()), ctx);
  }
}

}  // namespace foptim::optim
