#pragma once
#include <fmt/base.h>
#include <fmt/core.h>

#include <utility>

#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/function_ref.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/module_pass.hpp"
#include "utils/parameters.hpp"
#include "utils/set.hpp"

namespace foptim::optim {

namespace {
u64 arg_prom_unique_name_number = 0;
}

class ArgPromotion final : public ModulePass {
 public:
  bool are_there_potential_aliasing_stores(fir::FunctionR /*func*/,
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

  // if we return 2 vectors and then instant concat them we can also concat them
  // inside of the function
  bool return_vecvec_to_concat_vec(fir::FunctionR func, fir::Context &ctx) {
    const auto &func_ty = func->func_ty->as_func();
    if (!func_ty.return_type->is_struct()) {
      return false;
    }
    auto &elems = func_ty.return_type->as_struct().elems;
    if (elems.size() != 2 || !elems[0].ty->is_vec() ||
        elems[0].ty != elems[1].ty) {
      return false;
    }
    auto &inp_vec_ty = elems[0].ty->as_vec();
    if (inp_vec_ty.get_size() >= 64 ||
        (!utils::enable_avx512f && inp_vec_ty.get_size() >= 32)) {
      return false;
    }

    for (auto use : func->get_uses()) {
      if (use.type != fir::UseType::NormalArg ||
          !use.user->is(fir::InstrType::CallInstr) || use.argId != 0) {
        // fmt::println("FailCall");
        return false;
      }
      // if all uses are Extract values which are only used in concats
      for (auto suse : use.user->get_uses()) {
        if (!suse.user->is(fir::InstrType::ExtractValue)) {
          // fmt::println("FailExtract");
          return false;
        }
        auto exp_index = suse.user->args[1].as_constant()->as_int();
        for (auto ssuse : suse.user->get_uses()) {
          // TODO: could also handle shuffles
          if (!ssuse.user->is(fir::VectorISubType::Concat) ||
              ssuse.argId != exp_index) {
            // fmt::println("FailCOncat");
            return false;
          }
        }
      }
    }
    fir::TypeR res_type = ctx->get_vec_type(
        inp_vec_ty.type, inp_vec_ty.bitwidth, inp_vec_ty.member_number * 2);
    for (auto bb : func->basic_blocks) {
      auto term = bb->get_terminator();
      if (term->is(fir::InstrType::ReturnInstr)) {
        fir::Builder buh{term};
        ASSERT(term->args.size() == 2);
        auto res = buh.build_vector_op(term->args[0], term->args[1], res_type,
                                       fir::VectorISubType::Concat);
        term.remove_arg(1);
        term.replace_arg(0, res);
        term->value_type = res_type;
      }
    }
    fmt::println("Found");
    fmt::println("{:cd}", *func.func);
    for (auto use : func->get_uses()) {
      auto call_instr = use.user;
      for (auto suse : use.user->get_uses()) {
        for (auto ssuse : suse.user->get_uses()) {
          // replace the concat with the new return value
          ssuse.user->replace_all_uses(fir::ValueR{call_instr});
          ssuse.user.destroy();
        }
        suse.user.destroy();
      }
    }

    {  // type cleanup
      func.func->func_ty = ctx->get_func_ty(res_type, func_ty.arg_types);
      for (auto use : func.func->get_uses()) {
        use.user->extra_type = func.func->func_ty;
        use.user->value_type = res_type;
      }
    }
    if (func->attribs.linkage == fir::Linkage::LinkOnceODR) {
      auto old_name = func->name;
      arg_prom_unique_name_number++;
      auto new_name = old_name + "MODArgProm";
      new_name += std::to_string(arg_prom_unique_name_number);
      auto func_moved = std::move(ctx->storage.functions.at(old_name));
      ctx->storage.functions.erase(old_name);
      func_moved->name = new_name;
      func_moved->attribs.linkage = fir::Linkage::Internal;
      func_moved->attribs.no_inline = false;
      ctx->storage.functions.insert({new_name, std::move(func_moved)});
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

  // if we get vector args and all we do is concat them concat them prior and
  // only give in 1 arg
  bool promote_vecvec_to_concat_vec(fir::FunctionR func, fir::Context &ctx) {
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
          ctx->get_func_ty(func_ty.return_type, std::move(arg_tys));
      for (auto use : func.func->get_uses()) {
        use.user->extra_type = func.func->func_ty;
      }

      // // we need renaming
      if (func->attribs.linkage == fir::Linkage::LinkOnceODR) {
        auto old_name = func->name;
        arg_prom_unique_name_number++;
        auto new_name = old_name + "MODArgProm";
        new_name += std::to_string(arg_prom_unique_name_number);
        auto func_moved = std::move(ctx->storage.functions.at(old_name));
        ctx->storage.functions.erase(old_name);
        func_moved->name = new_name;
        func_moved->attribs.linkage = fir::Linkage::Internal;
        func_moved->attribs.no_inline = false;
        ctx->storage.functions.insert({new_name, std::move(func_moved)});
        return true;
      }
    }
    return false;
  }

  // if we have ptr arguments and all we do is a load of its value and its not a
  // massive value (<= ptrsize also good cause CC)
  //  we can isntead do the load before the call allowing potentialy more
  //  optimizations on that side like mem2reg
  bool promote_ptr_to_value_args(fir::FunctionR func, fir::Context &ctx) {
    CFG cfg{*func.func};
    AliasAnalyis aa;
    const auto &func_ty = func->func_ty->as_func();
    auto n_args_original = func_ty.arg_types.size();
    auto entry_block = func->get_entry();
    for (auto use : func->get_uses()) {
      if (use.type != fir::UseType::NormalArg ||
          !use.user->is(fir::InstrType::CallInstr) || use.argId != 0) {
        return false;
      }
    }

    // we can only do it tho iff the loads all have the same type
    //  we only load the value so no geps and similar stuff
    bool modified = false;
    IRVec<fir::TypeR> arg_tys;
    for (u64 i = n_args_original; i > 0; i--) {
      auto arg = entry_block->args[i - 1];
      if (arg->get_n_uses() == 0) {
        continue;
      }
      bool can_promote = arg->get_type()->is_ptr();
      fir::TypeR load_type = fir::TypeR();
      for (auto use : arg->uses) {
        // TODO there might be others??
        if (!use.user->is(fir::InstrType::LoadInstr) || use.user->Atomic) {
          can_promote = false;
          break;
        }
        if (!load_type.is_valid()) {
          load_type = use.user.get_type();
        } else if (load_type != use.user.get_type()) {
          can_promote = false;
          break;
        }
        if (!func->attribs.mem_read_none && !func->attribs.mem_read_only &&
            are_there_potential_aliasing_stores(func, arg, use, cfg, aa)) {
          can_promote = false;
          break;
        }
      }
      if (!can_promote || !load_type.is_valid()) {
        continue;
      }

      if (arg_tys.empty()) {
        // only make the copy if we know theres some thing we can modify
        arg_tys = func_ty.arg_types;
      }
      // TODO copy
      TVec<fir::Use> uses =
          TVec<fir::Use>{arg->get_uses().begin(), arg->get_uses().end()};
      for (auto use : uses) {
        use.user->replace_all_uses(fir::ValueR{arg});
      }
      for (auto use : uses) {
        use.user.destroy();
      }
      for (auto use : func->get_uses()) {
        fir::Builder b{use.user};
        auto load_result = b.build_load(load_type, use.user->args[1 + i - 1],
                                        use.user->Atomic, use.user->Volatile);
        use.user.replace_arg(1 + i - 1, load_result);
      }
      arg->_type = load_type;
      arg_tys[i - 1] = load_type;
      modified = true;
    }
    if (modified) {
      func.func->func_ty =
          ctx->get_func_ty(func_ty.return_type, std::move(arg_tys));
      for (auto use : func.func->get_uses()) {
        use.user->extra_type = func.func->func_ty;
      }

      // // we need renaming
      if (func->attribs.linkage == fir::Linkage::LinkOnceODR) {
        auto old_name = func->name;
        arg_prom_unique_name_number++;
        auto new_name = old_name + "MODArgProm";
        new_name += std::to_string(arg_prom_unique_name_number);
        auto func_moved = std::move(ctx->storage.functions.at(old_name));
        ctx->storage.functions.erase(old_name);
        func_moved->name = new_name;
        func_moved->attribs.linkage = fir::Linkage::Internal;
        func_moved->attribs.no_inline = false;
        ctx->storage.functions.insert({new_name, std::move(func_moved)});
        return true;
      }
    }
    return false;
  }

  void apply(fir::Context &ctx, JobSheduler * /*unused*/) override {
    ZoneScopedNC("ArgumentPromoition", COLOR_OPTIMM);
    auto iter = ctx.data->storage.functions.begin();
    for (; iter != ctx.data->storage.functions.end(); iter++) {
      auto &[_, f] = *iter;
      // fmt::println("RUNNING ON {}", f->name);
      // fmt::println("RUNNING ON {}", f->func_ty);
      // fmt::println("RUNNING ON {}", *f.get());
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

      const auto &func_ty = f->func_ty->as_func();
      auto n_args_original = func_ty.arg_types.size();
      // if we have no args skip
      if (n_args_original == 0) {
        continue;
      }

      // if any uses are not
      for (auto use : f->get_uses()) {
        if (!use.user->is(fir::InstrType::CallInstr) ||
            use.type != fir::UseType::NormalArg || use.argId != 0) {
          continue;
        }
      }
      if (promote_ptr_to_value_args(f.get(), ctx)) {
        iter = ctx.data->storage.functions.begin();
        continue;
      }
      if (promote_vecvec_to_concat_vec(f.get(), ctx)) {
        iter = ctx.data->storage.functions.begin();
        continue;
      }
      if (return_vecvec_to_concat_vec(f.get(), ctx)) {
        iter = ctx.data->storage.functions.begin();
        continue;
      }
    }
  }
};

}  // namespace foptim::optim
