#include "arg_promotion.hpp"

#include <utility>

#include "config/compiler_config.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "ir/value.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/helper/helper.hpp"
#include "utils/set.hpp"

namespace foptim::optim {

namespace {
u64 arg_prom_unique_name_number = 0;
}

bool ArgPromotion::are_there_potential_aliasing_stores(fir::FunctionR /*func*/,
                                                       fir::BBArgument /*barg*/,
                                                       fir::Use use, CFG &cfg,
                                                       AliasAnalyis &aa) {
  // iterate over parent bbs till entry
  //  if theres any writes that might alias we need to break
  // TODO: should prob move this out
  TSet<u32> visited;
  TVec<u32> worklist;
  worklist.push_back(cfg.get_bb_id(use.user->parent));
  ASSERT(use.user->is(fir::InstrType::LoadInstr));

  while (!worklist.empty()) {
    auto c = worklist.back();
    visited.insert(c);
    worklist.pop_back();
    for (auto p : cfg.bbrs[c].pred) {
      if (!visited.contains(c)) {
        worklist.push_back(p);
        visited.insert(p);
      }
    }

    for (auto i : cfg.bbrs[c].bb->instructions) {
      if (i == use.user) {
        // only up to our instruction
        break;
      }
      if (i->is(fir::InstrType::StoreInstr) &&
          aa.alias(i->args[0], use.user->args[0]) ==
              AliasAnalyis::AAResult::NoAlias) {
        continue;
      }
      if (i->pot_modifies_mem()) {
        return true;
      }
    }
  }
  return false;
}

bool ArgPromotion::return_vecvec_to_concat_vec(fir::FunctionR func,
                                               fir::Context &ctx,
                                               AnalysisManager &analyMan) {
  const auto &func_ty = func->func_ty->as_func();
  if (func_ty.n_results() != 2 || !func_ty.return_types[0]->is_vec() ||
      func_ty.return_types[0] != func_ty.return_types[1]) {
    return false;
  }
  const auto &inp_vec_ty = func_ty.return_types[0]->as_vec();
  if (inp_vec_ty.get_size() >= 64 ||
      (!ctx.config->target.features.avx512f && inp_vec_ty.get_size() >= 32)) {
    return false;
  }

  // every use of a result has to be the concat of both results
  for (auto use : func->get_uses()) {
    if (use.type != fir::UseType::NormalArg ||
        !use.user->is(fir::InstrType::CallInstr) || use.argId != 0) {
      return false;
    }
    for (auto suse : use.user->get_uses()) {
      if (!suse.user->is(fir::VectorISubType::Concat) ||
          suse.user->args[0] != use.user.result(0) ||
          suse.user->args[1] != use.user.result(1)) {
        return false;
      }
    }
  }
  fir::TypeR res_type = ctx->get_vec_type(inp_vec_ty.type, inp_vec_ty.bitwidth,
                                          inp_vec_ty.member_number * 2);
  for (auto bb : func->basic_blocks) {
    auto term = bb->get_terminator();
    if (term->is(fir::InstrType::ReturnInstr)) {
      fir::Builder buh{term};
      ASSERT(term->args.size() == 2);
      auto res = buh.build_vector_op(term->args[0], term->args[1], res_type,
                                     fir::VectorISubType::Concat);
      term.remove_arg(1);
      term.replace_arg(0, res);
      term->set_result_type(res_type);
    }
  }
  for (auto use : func->get_uses()) {
    auto call_instr = use.user;
    TVec<fir::Instr> concats;
    for (auto suse : call_instr->get_uses()) {
      if (std::ranges::find(concats, suse.user) == concats.end()) {
        concats.push_back(suse.user);
      }
    }
    // the concat becomes the only result of the call
    for (auto concat : concats) {
      concat.result().replace_all_uses(call_instr.result(0));
      concat.destroy();
    }
    fir::TypeR new_res[1] = {res_type};
    call_instr->set_result_types(new_res);
  }

  {  // type cleanup
    func.func->func_ty = ctx->get_func_ty(res_type, func_ty.arg_types);
    for (auto use : func.func->get_uses()) {
      use.user->extra_type = func.func->func_ty;
    }
  }
  analyMan.touched_signature(*func.func);
  if (func->attribs.linkage == fir::Linkage::LinkOnceODR) {
    auto old_name = func->name;
    arg_prom_unique_name_number++;
    auto new_name = old_name + "MODArgProm";
    new_name += std::to_string(arg_prom_unique_name_number);
    auto *func_renamed = ctx->storage.functions.get(old_name.c_str());
    ctx->storage.functions.rename(old_name.c_str(), new_name);
    func_renamed->attribs.linkage = fir::Linkage::Internal;
    func_renamed->attribs.no_inline = false;
  }
  // fmt::println("Found");
  // fmt::println("{:cd}", *func.func);
  // for (auto use : func->get_uses()) {
  //   auto call_instr = use.user;
  //   fmt::println("{:cd}", call_instr->get_parent());
  // }
  // TODO("impl");
  // TODO("impl cleanup");
  return true;
}

bool ArgPromotion::promote_vecvec_to_concat_vec(fir::FunctionR func,
                                                fir::Context &ctx,
                                                AnalysisManager &analyMan) {
  const auto &func_ty = func->func_ty->as_func();
  auto n_args_original = func_ty.arg_types.size();
  auto entry_block = func->get_entry();
  // CFG cfg{*func.func};
  for (auto use : func->get_uses()) {
    if (use.type != fir::UseType::NormalArg ||
        !use.user->is(fir::InstrType::CallInstr) || use.argId != 0) {
      return false;
    }
  }
  bool modified = false;
  IRVec<fir::TypeR> arg_tys;
  TVec<fir::BBArgument> args;
  for (u64 i = n_args_original; i > 0; i--) {
    auto arg = entry_block->args[i - 1];
    if (arg->get_n_uses() == 0 || !arg->get_type()->is_vec()) {
      continue;
    }
    bool can_promote = false;
    fir::BBArgument other_arg = fir::BBArgument{fir::BBArgument::invalid()};
    for (auto use : arg->uses) {
      if (use.type != fir::UseType::NormalArg ||
          !use.user->is(fir::VectorISubType::Concat)) {
        can_promote = false;
        break;
      }
      auto other_arg_id = 1 - use.argId;
      if (use.user->args[other_arg_id].is_bb_arg() &&
          use.user->args[other_arg_id].as_bb_arg()->get_parent() ==
              entry_block &&
          (!other_arg.is_valid() ||
           use.user->args[other_arg_id].as_bb_arg() == other_arg)) {
        other_arg = use.user->args[other_arg_id].as_bb_arg();
      } else {
        can_promote = false;
        break;
      }
    }
    if (!can_promote) {
      continue;
    }

    fmt::println("{:cd}", arg);
    fmt::println("{:cd}", other_arg);
    TODO("Impl");
    modified = true;
  }
  if (modified) {
    func.func->func_ty =
        ctx->get_func_ty(func_ty.return_types, std::move(arg_tys));
    for (auto use : func.func->get_uses()) {
      use.user->extra_type = func.func->func_ty;
    }
    analyMan.touched_signature(*func.func);

    // // we need renaming
    if (func->attribs.linkage == fir::Linkage::LinkOnceODR) {
      auto old_name = func->name;
      arg_prom_unique_name_number++;
      auto new_name = old_name + "MODArgProm";
      new_name += std::to_string(arg_prom_unique_name_number);
      auto *func_renamed = ctx->storage.functions.get(old_name.c_str());
      ctx->storage.functions.rename(old_name.c_str(), new_name);
      func_renamed->attribs.linkage = fir::Linkage::Internal;
      func_renamed->attribs.no_inline = false;
      return true;
    }
  }
  return false;
}

bool ArgPromotion::promote_ptr_to_value_return(fir::FunctionR func,
                                               fir::Context &ctx,
                                               AnalysisManager &analyMan) {
  if (func->func_ty->as_func().n_results() != 1 ||
      !func->func_ty->as_func().return_type()->is_ptr()) {
    return false;
  }

  fir::TypeR res_type{};
  for (auto use : func->get_uses()) {
    // check if used outside of direct calls
    if (use.type != fir::UseType::NormalArg ||
        !use.user->is(fir::InstrType::CallInstr) || use.argId != 0) {
      return false;
    }
    // check if return value is only loaded
    for (auto ret_use : use.user->uses) {
      const auto load_i = ret_use.user;
      if (!load_i->is(fir::InstrType::LoadInstr)) {
        return false;
      }
      // TODO: could handle this however there are some issues
      //  when for example multiple volatiles get joined or atomics would need
      //  to check some ordering
      if (load_i->Atomic || load_i->Volatile) {
        return false;
      }
      // needs consistent typing
      if (!res_type.is_valid()) {
        res_type = load_i.result_type();
      } else if (res_type != load_i.result_type()) {
        return false;
      }
      // need to verify no writes inbetween
      // TODO: for now just most basic approach should do AA analysis
      {
        // for basic appraoch just check if same bb + no potential writes
        // between
        if (load_i->get_parent() != use.user->get_parent()) {
          return false;
        }
        auto bb = use.user->get_parent();
        bool found_write = false;
        for (size_t i = 0; i < bb->instructions.size(); i++) {
          if (bb->instructions[i] == use.user) {
            found_write = false;
          } else if (bb->instructions[i]->pot_modifies_mem()) {
            found_write = true;
          } else if (bb->instructions[i] == load_i) {
            break;
          }
        }
        if (found_write) {
          return false;
        }
      }
    }
  }
  if (!res_type.is_valid()) {
    return false;
  }
  // return false;

  // remove the loads from the function calls
  for (auto use : func->get_uses()) {
    for (auto ret_use : use.user->uses) {
      ret_use.user->replace_all_uses(use.user.result());
      ret_use.user.destroy();
    }
  }

  for (auto bb : func->basic_blocks) {
    auto term = bb->get_terminator();
    if (!term->is(fir::InstrType::ReturnInstr)) {
      continue;
    }
    fir::Builder buh(term);
    auto new_loaded_val = buh.build_load(res_type, term->args[0], false, false);
    term.replace_arg(0, new_loaded_val);
  }
  (void)ctx;
  // fmt::print("{:cd}", *func.func);
  // for (auto use : func->get_uses()) {
  //   fmt::print("{:cd}", use.user->get_parent());
  // }
  // TODO("impl");
  const auto &func_ty = func.func->func_ty->as_func();
  func.func->func_ty = ctx->get_func_ty(res_type, func_ty.arg_types);
  for (auto use : func.func->get_uses()) {
    use.user->extra_type = func.func->func_ty;
    use.user->set_result_type(res_type);
  }
  analyMan.touched_signature(*func.func);

  // we need renaming
  if (func->attribs.linkage == fir::Linkage::LinkOnceODR) {
    auto old_name = func->name;
    arg_prom_unique_name_number++;
    auto new_name = old_name + "MODArgProm";
    new_name += std::to_string(arg_prom_unique_name_number);
    auto *func_renamed = ctx->storage.functions.get(old_name.c_str());
    ctx->storage.functions.rename(old_name.c_str(), new_name);
    func_renamed->attribs.linkage = fir::Linkage::Internal;
    func_renamed->attribs.no_inline = false;
    return true;
  }

  return true;
}

bool ArgPromotion::all_exits_are_dominated(const CFG &cfg,
                                           const Dominators &dom,
                                           const TVec<u32> &direct_load_bbs) {
  // for now just assume every block that has no successor is terminator
  //  this isnt quite true with unreach but shouldnt affect many cases

  for (size_t exit_bb_id = 0; exit_bb_id < cfg.bbrs.size(); exit_bb_id++) {
    if (!cfg.bbrs[exit_bb_id].succ.empty()) {
      continue;
    }
    bool any_dom = false;
    for (auto bb_id : direct_load_bbs) {
      if (dom.strict_dominates(bb_id, exit_bb_id)) {
        any_dom = true;
        break;
      }
    }
    if (!any_dom) {
      return false;
    }
  }
  return true;
}

namespace {
enum class PtrArgType {
  None,
  ReadOnlyPtr,
  WriteOnlyPtr,
  ReadWritePtr,
};

struct PtrArgInfo {
  PtrArgType kind = PtrArgType::None;
  // type of every load and store through the pointer
  fir::TypeR type{};
  // the pointer is only used through selects that feed loads
  bool through_select = false;
};

// Looks only at how the pointer is used inside of the function, the checks
// that depend on aliasing, dominance or the call sites are up to the caller.
PtrArgInfo classify_ptr_arg(fir::BBArgument arg) {
  PtrArgInfo info;
  if (!arg->get_type()->is_ptr() || arg->get_n_uses() == 0) {
    return info;
  }
  bool has_load = false;
  bool has_store = false;
  // false if the types of the accesses differ
  const auto merge_type = [&](fir::TypeR ty) {
    if (!info.type.is_valid()) {
      info.type = ty;
    }
    return info.type == ty;
  };
  for (auto use : arg->uses) {
    const auto user = use.user;
    if (use.type != fir::UseType::NormalArg) {
      return {};
    }
    if (user->is(fir::InstrType::LoadInstr)) {
      if (user->Atomic || user->Volatile || !merge_type(user.result_type())) {
        return {};
      }
      has_load = true;
    } else if (user->is(fir::InstrType::StoreInstr)) {
      // the pointer itself stored somewhere escapes
      if (use.argId != 0 || user->Atomic || user->Volatile ||
          !merge_type(user->args[1].get_type())) {
        return {};
      }
      has_store = true;
    } else if (user->is(fir::InstrType::SelectInstr) && use.argId != 0) {
      for (auto sub_use : user->uses) {
        const auto sub = sub_use.user;
        if (!sub->is(fir::InstrType::LoadInstr) || sub_use.argId != 0 ||
            sub->Atomic || sub->Volatile || !merge_type(sub.result_type())) {
          return {};
        }
      }
      info.through_select = true;
      has_load = true;
    } else {
      return {};
    }
  }
  // stores through a select would need both targets written
  if (info.through_select && has_store) {
    return {};
  }
  if (!info.type.is_valid()) {
    return {};
  }
  info.kind = has_load && has_store ? PtrArgType::ReadWritePtr
              : has_store           ? PtrArgType::WriteOnlyPtr
                                    : PtrArgType::ReadOnlyPtr;
  return info;
}
}  // namespace

namespace {
// Types that travel as one value through the argument and result registers.
bool is_promotable_value_type(fir::TypeR ty) {
  if (ty->is_int()) {
    return ty->as_int() <= 64;
  }
  return ty->is_ptr() || ty->is_f32() || ty->is_f64();
}

// Writes through the pointer can be turned into a returned value if nothing
// else can observe the memory during the call: the pointer is a private stack
// slot that is never captured and not passed twice.
bool is_private_alloca_arg(fir::FunctionR func, fir::Instr call, u32 pos,
                           const TVec<PtrArgInfo> &raw) {
  const auto ptr = call->args[1 + pos];
  if (!ptr.is_instr() || !ptr.as_instr()->is(fir::InstrType::AllocaInstr)) {
    return false;
  }
  for (u32 j = 1; j < call->args.size(); j++) {
    if (j != 1 + pos && call->args[j] == ptr) {
      return false;
    }
  }
  for (auto use : ptr.as_instr()->get_uses()) {
    const auto user = use.user;
    if (use.type != fir::UseType::NormalArg) {
      return false;
    }
    if (user->is(fir::InstrType::LoadInstr) ||
        (user->is(fir::InstrType::StoreInstr) && use.argId == 0)) {
      if (user->Atomic || user->Volatile) {
        return false;
      }
      continue;
    }
    // handing it to the function itself is fine as long as the callee only
    // loads and stores through that argument
    if (user->is(fir::InstrType::CallInstr) && use.argId >= 1 &&
        user->args[0].is_constant() && user->args[0].as_constant()->is_func() &&
        user->args[0].as_constant()->as_func().func == func.func &&
        raw[use.argId - 1].kind != PtrArgType::None) {
      continue;
    }
    return false;
  }
  return true;
}

}  // namespace

bool ArgPromotion::promote_ptr_args(fir::FunctionR func, fir::Context &ctx,
                                    AnalysisManager &analyMan) {
  const auto &func_ty = func->func_ty->as_func();
  const auto n_args = func_ty.arg_types.size();
  auto entry_block = func->get_entry();
  // only if we know all call sites
  TVec<fir::Use> calls;
  for (auto use : func->get_uses()) {
    if (use.type != fir::UseType::NormalArg ||
        !use.user->is(fir::InstrType::CallInstr) || use.argId != 0) {
      return false;
    }
    calls.push_back(use);
  }
  CFG cfg{*func.func};
  Dominators dom{cfg};
  AliasAnalyis aa;

  TVec<PtrArgInfo> raw;
  raw.resize(n_args);
  bool any_candidate = false;
  for (u64 i = 0; i < n_args; i++) {
    raw[i] = classify_ptr_arg(entry_block->args[i]);
    any_candidate |= raw[i].kind != PtrArgType::None;
  }
  if (!any_candidate) {
    return false;
  }

  // read only: the load moves in front of the call, the value must not be
  // changed by a store in between and has to be loaded on every path
  const auto readonly_ok = [&](fir::BBArgument arg) {
    const bool no_writes =
        func->attribs.mem_read_none || func->attribs.mem_read_only;
    TVec<u32> direct_load_bbs;
    for (auto use : arg->uses) {
      if (use.user->is(fir::InstrType::LoadInstr)) {
        if (!no_writes &&
            are_there_potential_aliasing_stores(func, arg, use, cfg, aa)) {
          return false;
        }
        direct_load_bbs.push_back(cfg.get_bb_id(use.user->get_parent()));
        continue;
      }
      for (auto sub_use : use.user->uses) {
        if (!no_writes &&
            are_there_potential_aliasing_stores(func, arg, sub_use, cfg, aa)) {
          return false;
        }
      }
    }
    return all_exits_are_dominated(cfg, dom, direct_load_bbs);
  };

  // written: value in, final value out. Results are limited by the return
  // registers.
  u32 n_int_res = 0;
  u32 n_sse_res = 0;
  const auto count_result = [&](fir::TypeR ty) {
    if (ty->is_void()) {
      return;
    }
    (ty->is_int_or_ptr() ? n_int_res : n_sse_res)++;
  };
  for (auto ty : func_ty.return_types) {
    count_result(ty);
  }
  const auto inout_ok = [&](u64 i) {
    if (!is_promotable_value_type(raw[i].type)) {
      return false;
    }
    for (auto use : calls) {
      if (!is_private_alloca_arg(func, use.user, static_cast<u32>(i), raw)) {
        return false;
      }
    }
    const bool is_int = raw[i].type->is_int_or_ptr();
    if ((is_int ? n_int_res : n_sse_res) >= 2) {
      return false;
    }
    count_result(raw[i].type);
    return true;
  };

  TVec<PtrArgInfo> chosen;
  chosen.resize(n_args);
  bool any_inout = false;
  bool modified = false;
  for (u64 i = 0; i < n_args; i++) {
    switch (raw[i].kind) {
      case PtrArgType::None:
        break;
      case PtrArgType::ReadOnlyPtr:
        if (readonly_ok(entry_block->args[i])) {
          chosen[i] = raw[i];
        }
        break;
      case PtrArgType::WriteOnlyPtr:
      case PtrArgType::ReadWritePtr:
        if (inout_ok(i)) {
          chosen[i] = raw[i];
          any_inout = true;
        }
        break;
    }
    modified |= chosen[i].kind != PtrArgType::None;
  }
  if (!modified) {
    return false;
  }

  IRVec<fir::TypeR> arg_tys = func_ty.arg_types;
  IRVec<fir::TypeR> ret_tys = func_ty.return_types;
  const u32 n_orig_results = func_ty.n_results();
  TVec<u64> inout_args;
  if (any_inout && ret_tys.size() == 1 && ret_tys[0]->is_void()) {
    ret_tys.clear();
  }
  for (u64 i = 0; i < n_args; i++) {
    if (chosen[i].kind == PtrArgType::WriteOnlyPtr ||
        chosen[i].kind == PtrArgType::ReadWritePtr) {
      inout_args.push_back(i);
      ret_tys.push_back(chosen[i].type);
    }
  }

  // callee
  TVec<fir::ValueR> slots;
  for (u64 i = 0; i < n_args; i++) {
    const auto kind = chosen[i].kind;
    if (kind == PtrArgType::None) {
      continue;
    }
    auto arg = entry_block->args[i];
    const auto load_type = chosen[i].type;
    arg->_type = load_type;
    arg->noalias = 0;
    arg->get_attribs().clear();
    if (kind == PtrArgType::ReadOnlyPtr) {
      TVec<fir::Use> uses =
          TVec<fir::Use>{arg->get_uses().begin(), arg->get_uses().end()};
      for (auto use : uses) {
        if (use.user->is(fir::InstrType::LoadInstr)) {
          use.user->replace_all_uses(fir::ValueR{arg});
        } else {
          fir::Builder buh{use.user};
          // the promoted argument already is the loaded value
          auto load_of = [&](fir::ValueR ptr) {
            if (ptr == fir::ValueR{arg}) {
              return fir::ValueR{arg};
            }
            return buh.build_load(load_type, ptr, false, false);
          };
          auto load1 = load_of(use.user->args[1]);
          auto load2 = load_of(use.user->args[2]);
          auto res =
              buh.build_select(load_type, use.user->args[0], load1, load2);
          for (auto subuse : use.user->uses) {
            subuse.user->replace_all_uses(res);
            subuse.user.destroy();
          }
        }
      }
      for (auto use : uses) {
        use.user.destroy();
      }
    } else {
      // the pointer becomes a private slot that holds the incoming value,
      // Mem2Reg turns it hopefullyy into registers
      fir::Builder buh{entry_block};
      buh.at_start(entry_block);
      auto slot = buh.build_alloca(
          fir::ValueR(ctx->get_constant_int(load_type->get_size(), 32)));
      slot.as_instr()->extra_type = load_type;
      fir::ValueR{arg}.replace_all_uses(slot);
      buh.build_store(slot, fir::ValueR{arg}, false, false);
      slots.push_back(slot);
    }
    arg_tys[i] = load_type;
  }
  if (any_inout) {
    for (auto bb : func->basic_blocks) {
      auto term = bb->get_terminator();
      if (!term->is(fir::InstrType::ReturnInstr)) {
        continue;
      }
      fir::Builder buh{term};
      for (size_t k = 0; k < inout_args.size(); k++) {
        term.add_arg(
            buh.build_load(chosen[inout_args[k]].type, slots[k], false, false));
      }
      term->set_result_type(ret_tys[0]);
    }
  }

  func.func->func_ty = ctx->get_func_ty(ret_tys, std::move(arg_tys));

  // fix up dat call sites
  for (auto use : calls) {
    auto call = use.user;
    TVec<fir::ValueR> out_ptrs;
    {
      fir::Builder buh{call};
      for (u64 i = 0; i < n_args; i++) {
        if (chosen[i].kind == PtrArgType::None) {
          continue;
        }
        const auto ptr = call->args[1 + i];
        if (chosen[i].kind != PtrArgType::ReadOnlyPtr) {
          out_ptrs.push_back(ptr);
        }
        call.replace_arg(1 + i,
                         buh.build_load(chosen[i].type, ptr, false, false));
      }
    }
    call->extra_type = func.func->func_ty;
    if (any_inout) {
      call->set_result_types(ret_tys);
      fir::Builder buh{call};
      buh.after(call);
      for (size_t k = 0; k < out_ptrs.size(); k++) {
        buh.build_store(out_ptrs[k],
                        call.result(static_cast<u8>(n_orig_results + k)), false,
                        false);
      }
    }
  }
  analyMan.touched_signature(*func.func);

  if (func->attribs.linkage == fir::Linkage::LinkOnceODR) {
    auto old_name = func->name;
    arg_prom_unique_name_number++;
    auto new_name = old_name + "MODArgProm";
    new_name += std::to_string(arg_prom_unique_name_number);
    auto *func_renamed = ctx->storage.functions.get(old_name.c_str());
    ctx->storage.functions.rename(old_name.c_str(), new_name);
    func_renamed->attribs.linkage = fir::Linkage::Internal;
    func_renamed->attribs.no_inline = false;
  }
  return true;
}

PreservedAnalysis ArgPromotion::apply(fir::Context &ctx,
                                      JobSheduler * /*unused*/,
                                      AnalysisManager &analyMan) {
  ZoneScopedNC("ArgumentPromoition", COLOR_OPTIMM);
  // A function whose signature changed gets loads in front of its calls, which
  // can make arguments of its callers promotable. Only those callers (and the
  // function itself) are looked at again.
  constexpr u32 MaxVisits = 8;
  TVec<fir::Function *> work;
  TSet<fir::Function *> queued;
  TMap<fir::Function *, u32> visits;
  for (auto *f : ctx.data->storage.functions.all()) {
    work.push_back(f);
    queued.insert(f);
  }
  const auto enqueue = [&](fir::Function *f) {
    if (visits[f] < MaxVisits && queued.insert(f).second) {
      work.push_back(f);
    }
  };
  const auto address_taken = functions_referenced_by_globals(ctx);
  for (size_t head = 0; head < work.size(); head++) {
    auto *f = work[head];
    queued.erase(f);
    visits[f]++;
    if (address_taken.contains(f)) {
      continue;
    }
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
    // if we have no args skip
    if (f->func_ty->as_func().arg_types.empty()) {
      continue;
    }

    const auto old_func_ty = f->func_ty;
    // the returns only say if the function got renamed, every transformation
    // reports what it changed itself
    (void)(promote_ptr_to_value_return(f, ctx, analyMan) ||
           promote_ptr_args(f, ctx, analyMan) ||
           promote_vecvec_to_concat_vec(f, ctx, analyMan) ||
           return_vecvec_to_concat_vec(f, ctx, analyMan));
    if (f->func_ty == old_func_ty) {
      continue;
    }
    enqueue(f);
    for (auto use : f->get_uses()) {
      enqueue(use.user->get_parent()->get_parent().func);
    }
  }
  return PreservedAnalysis::all();
}
}  // namespace foptim::optim
