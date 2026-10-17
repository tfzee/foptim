#include "IPCP.hpp"

#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "optim/analysis/analysis_manager.hpp"

namespace foptim::optim {
namespace {

u64 ipcp_unique_name_number = 0;

bool is_direct_call_use(const fir::Use &use) {
  return use.type == fir::UseType::NormalArg && use.argId == 0 &&
         use.user->is(fir::InstrType::CallInstr);
}

// the result i of a call of func itself, as returned by a return of func
bool is_self_forward(fir::ValueR v, u8 i, fir::FunctionR func) {
  if (!v.is_instr() || v.idx != i || !v.as_instr()->is(fir::InstrType::CallInstr)) {
    return false;
  }
  auto callee = v.as_instr()->args[0];
  return callee.is_constant() && callee.as_constant()->is_func() &&
         callee.as_constant()->as_func().func == func.func;
}

// Results that are the same constant (or entry argument) on every return are
// replaced in the callers, a result that only forwards the recursive call does
// not count as a different value.
bool constant_prop_return(fir::FunctionR func, const TVec<fir::Instr> &rets,
                          AnalysisManager &analyMan) {
  const auto n_results = func->func_ty->as_func().n_results();
  bool modified = false;
  for (u8 i = 0; i < n_results; i++) {
    bool same = true;
    bool found = false;
    bool forwards = false;
    fir::ValueR val;
    for (auto ret : rets) {
      auto v = ret->args[i];
      if (is_self_forward(v, i, func)) {
        forwards = true;
        continue;
      }
      if (!v.is_constant() &&
          (!v.is_bb_arg() || v.as_bb_arg()->get_parent() != func->get_entry())) {
        same = false;
        break;
      }
      if (found && v != val) {
        same = false;
        break;
      }
      val = v;
      found = true;
    }
    // an argument has another value in the recursive call, only a constant
    // is the same in all the invocations
    if (!same || !found || (forwards && !val.is_constant())) {
      continue;
    }
    for (auto use : func->get_uses()) {
      if (!is_direct_call_use(use) || use.user.result(i).get_n_uses() == 0) {
        continue;
      }
      auto repl = val.is_constant()
                      ? val
                      : use.user->args[func->get_entry()->get_arg_id(
                                           val.as_bb_arg()) +
                                       1];
      use.user.result(i).replace_all_uses(repl);
      modified = true;
    }
  }
  if (modified) {
    // only the uses of the call results in the callers were rewritten
    analyMan.touched_callers(*func.func);
  }
  return modified;
}

// Results that are the same value on every return are merged into the first
// one, the callers use that result for both.
bool merge_equal_results(fir::FunctionR func, const TVec<fir::Instr> &rets,
                         AnalysisManager &analyMan) {
  const auto n_results = func->func_ty->as_func().n_results();
  bool modified = false;
  for (u8 j = 1; j < n_results; j++) {
    for (u8 i = 0; i < j; i++) {
      bool same = true;
      for (auto ret : rets) {
        if (ret->args[i] != ret->args[j]) {
          same = false;
          break;
        }
      }
      if (!same) {
        continue;
      }
      for (auto use : func->get_uses()) {
        if (is_direct_call_use(use) && use.user.result(j).get_n_uses() != 0) {
          use.user.result(j).replace_all_uses(use.user.result(i));
          modified = true;
        }
      }
      break;
    }
  }
  if (modified) {
    analyMan.touched_callers(*func.func);
  }
  return modified;
}

// A result is dead if no caller reads it, the recursive call whose result is
// only returned at the same position does not count as a read.
bool result_is_dead(fir::FunctionR func, u8 i) {
  for (auto use : func->get_uses()) {
    auto call = use.user;
    const bool rec = call->get_parent()->get_parent() == func.func;
    u32 forwarded = 0;
    if (rec) {
      for (auto u : call->get_uses()) {
        if (u.type == fir::UseType::NormalArg &&
            u.user->is(fir::InstrType::ReturnInstr) &&
            u.user->get_parent()->get_parent() == func.func && u.argId == i &&
            u.user->args[i] == call.result(i)) {
          forwarded++;
        }
      }
    }
    if (call.result(i).get_n_uses() != forwarded) {
      return false;
    }
  }
  return true;
}

// Drops results nobody reads from the function, its returns and its calls.
bool kill_dead_results(fir::FunctionR func, fir::Context &ctx,
                       const TVec<fir::Instr> &rets, AnalysisManager &analyMan) {
  const auto func_ty = func->func_ty->as_func();
  const auto n_results = func_ty.n_results();
  TVec<u8> new_index(n_results);
  IRVec<fir::TypeR> new_rets;
  bool any_dead = false;
  for (u8 i = 0; i < n_results; i++) {
    if (result_is_dead(func, i)) {
      any_dead = true;
      new_index[i] = 255;
    } else {
      new_index[i] = static_cast<u8>(new_rets.size());
      new_rets.push_back(func_ty.return_types[i]);
    }
  }
  if (!any_dead) {
    return false;
  }
  for (auto ret : rets) {
    for (u8 i = n_results; i > 0; i--) {
      if (new_index[i - 1] == 255) {
        ret.remove_arg(i - 1);
      }
    }
    ret->set_result_type(ret->args.empty() ? ctx->get_void_type()
                                           : ret->args[0].get_type());
  }
  const bool no_results = new_rets.empty();
  if (no_results) {
    new_rets.push_back(ctx->get_void_type());
  }
  func.func->func_ty = ctx->get_func_ty(new_rets, func_ty.arg_types);
  for (auto use : func->get_uses()) {
    auto call = use.user;
    // ascending, a result only moves to a lower index
    for (u8 j = 0; j < n_results; j++) {
      if (new_index[j] != 255 && new_index[j] != j) {
        call.result(j).replace_all_uses(call.result(new_index[j]));
      }
    }
    call->set_result_types(new_rets);
    call->extra_type = func.func->func_ty;
  }
  analyMan.touched_signature(*func.func);
  if (func->attribs.linkage == fir::Linkage::LinkOnceODR ||
      func->getName().starts_with("foptim.")) {
    auto old_name = func->name;
    ipcp_unique_name_number++;
    auto new_name = old_name + "MODIPCP";
    new_name += std::to_string(ipcp_unique_name_number);
    ASSERT(!ctx->storage.functions.contains(new_name.c_str()));
    auto *func_renamed = ctx->storage.functions.get(old_name.c_str());
    ctx->storage.functions.rename(old_name.c_str(), new_name);
    func_renamed->attribs.linkage = fir::Linkage::Internal;
    func_renamed->attribs.no_inline = false;
  }
  return true;
}

void optimize_returns(fir::FunctionR func, fir::Context &ctx,
                      AnalysisManager &analyMan) {
  if (func->func_ty->as_func().n_results() == 0 || func->get_n_uses() == 0) {
    return;
  }
  TVec<fir::Instr> rets;
  for (auto bb : func->basic_blocks) {
    for (auto instr : bb->instructions) {
      if (instr->is(fir::InstrType::ReturnInstr)) {
        rets.push_back(instr);
      }
    }
  }
  if (rets.empty()) {
    return;
  }
  constant_prop_return(func, rets, analyMan);
  merge_equal_results(func, rets, analyMan);
  kill_dead_results(func, ctx, rets, analyMan);
}

bool constant_prop_args(fir::FunctionR func, fir::Context &ctx,
                        AnalysisManager &analyMan) {
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
      // recursive case
      if (use.user->get_parent()->get_parent() == func.func &&
          use.user->args[i].is_bb_arg() &&
          use.user->args[i].as_bb_arg() == entry_block->args[i - 1]) {
        continue;
      }
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

    // only recursive calls pass the argument on, nothing to propagate
    if (!consti.is_valid()) {
      can_convert = false;
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
      arg_tys.erase(arg_tys.begin() + static_cast<i64>(i - 1));
      // fmt::println("{:cd}", *func.func);
      modified = true;
    }
  }
  if (modified) {
    func.func->func_ty =
        ctx->get_func_ty(func_ty.return_types, std::move(arg_tys));
    for (auto use : func.func->get_uses()) {
      use.user->extra_type = func.func->func_ty;
    }
    analyMan.touched_signature(*func.func);
    // we need renaming
    if (func->attribs.linkage == fir::Linkage::LinkOnceODR ||
        func->getName().starts_with("foptim.")) {
      auto old_name = func->name;
      ipcp_unique_name_number++;
      auto new_name = old_name + "MODIPCP";
      new_name += std::to_string(ipcp_unique_name_number);
      ASSERT(!ctx->storage.functions.contains(new_name.c_str()));
      auto *func_renamed = ctx->storage.functions.get(old_name.c_str());
      ctx->storage.functions.rename(old_name.c_str(), new_name);
      func_renamed->attribs.linkage = fir::Linkage::Internal;
      func_renamed->attribs.no_inline = false;
      return true;
    }
  }
  return false;
}

bool kill_dead_args(fir::FunctionR func, fir::Context &ctx,
                    AnalysisManager &analyMan) {
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
        ctx->get_func_ty(func_ty.return_types, std::move(arg_tys));
    for (auto use : func.func->get_uses()) {
      use.user->extra_type = func.func->func_ty;
    }
    analyMan.touched_signature(*func.func);

    if (func->attribs.linkage == fir::Linkage::LinkOnceODR ||
        func->getName().starts_with("foptim.")) {
      auto old_name = func->name;
      ipcp_unique_name_number++;
      auto new_name = old_name + "MODIPCP";
      new_name += std::to_string(ipcp_unique_name_number);
      ASSERT(!ctx->storage.functions.contains(new_name.c_str()));
      auto *func_renamed = ctx->storage.functions.get(old_name.c_str());
      ctx->storage.functions.rename(old_name.c_str(), new_name);
      func_renamed->attribs.linkage = fir::Linkage::Internal;
      // TODO: Technically legal but might be counter productive
      func_renamed->attribs.no_inline = false;
      return true;
    }
  }
  return false;
}
} // namespace

PreservedAnalysis IPCP::apply(fir::Context &ctx, JobSheduler * /*unused*/,
                              AnalysisManager &analyMan) {
  ZoneScopedN("IPCP");
  // copy since passes below may add functions
  const auto funcs = ctx.data->storage.functions.all();
  for (auto *f : funcs) {
    switch (f->attribs.linkage) {
    case fir::Linkage::External:
    case fir::Linkage::Weak:
    case fir::Linkage::LinkOnce:
    case fir::Linkage::WeakODR:
      continue;
    case fir::Linkage::LinkOnceODR:
    case fir::Linkage::Internal:
      break;
    }

    if (f->is_decl() || f->attribs.variadic) {
      continue;
    }
    bool skip = false;
    for (auto use : f->get_uses()) {
      if (!use.user->is(fir::InstrType::CallInstr) ||
          use.type != fir::UseType::NormalArg || use.argId != 0) {
        skip = true;
        break;
      }
    }
    if (skip) {
      continue;
    }

    // if (constant_prop_args(fir::FunctionR(f), ctx)) {
    //   continue;
    // }
    // each transformation reports what it changed (f and its call sites), the
    // returns only say if the function got renamed
    if (!kill_dead_args(f, ctx, analyMan) &&
        !constant_prop_args(fir::FunctionR(f), ctx, analyMan)) {
      optimize_returns(fir::FunctionR(f), ctx, analyMan);
    }
  }
  return PreservedAnalysis::all();
}

} // namespace foptim::optim
