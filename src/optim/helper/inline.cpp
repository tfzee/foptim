#include "inline.hpp"

#include "ir/basic_block.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/mem/alias_sets.hpp"
#include "utils/map.hpp"

namespace foptim::optim {

namespace {
// Every inlined call gets scopes of its own, and every noalias parameter of
// the callee is a scope. An access only based on the parameter is in it, one
// that cannot be based on it does not alias it. new_bbs are the copies of the
// callee blocks.
void scope_inlined_accesses(fir::ContextData *ctx, fir::Function &callee,
                            fir::Function &caller,
                            std::span<const fir::BasicBlock> new_bbs) {
  auto &tab = ctx->alias_sets();
  using T = AliasSetTable;
  FDMap<SetId, SetId> fresh;
  auto fresh_leaf = [&](SetId leaf) {
    auto it = fresh.find(leaf);
    if (it == fresh.end()) {
      it = fresh.emplace(leaf, tab.create(std::span<const SetId>{})).first;
    }
    return it->second;
  };
  auto remap = [&](SetId s) {
    if (!tab.is_union(s)) {
      return fresh_leaf(s);
    }
    const FVec<SetId> parts(tab.parts(s).begin(), tab.parts(s).end());
    SetId r = 0;
    bool first = true;
    for (auto p : parts) {
      const SetId m = fresh_leaf(p);
      r = first ? m : tab.union_of(r, m);
      first = false;
    }
    return r;
  };

  struct Restrict {
    SetId object;
    SetId scope;
    bool nocapture;
  };
  FVec<Restrict> restricts;
  Provenance &prov = AnalysisManager::provenance(callee);
  for (const auto &arg : callee.get_entry()->args) {
    if (arg->noalias && arg->_type->is_ptr()) {
      restricts.push_back({.object = prov.set_of(fir::ValueR{arg}),
                           .scope = tab.create(std::span<const SetId>{}),
                           .nocapture = arg->nocapture != 0});
    }
  }
  if (restricts.empty() && !caller.access_meta) {
    return;
  }

  const auto &ptab = prov.table();
  auto add = [](IRVec<u32> &v, u32 x) {
    if (std::ranges::find(v, x) == v.end()) {
      v.push_back(x);
    }
  };
  for (size_t b = 0; b < new_bbs.size(); b++) {
    auto old_bb = callee.basic_blocks.at(b);
    auto new_bb = new_bbs[b];
    ASSERT(old_bb->instructions.size() == new_bb->instructions.size());
    for (size_t k = 0; k < old_bb->instructions.size(); k++) {
      auto old_i = old_bb->instructions[k];
      auto new_i = new_bb->instructions[k];
      if (!old_i->is(fir::InstrType::LoadInstr) &&
          !old_i->is(fir::InstrType::StoreInstr)) {
        continue;
      }
      fir::AccessMeta m;
      if (const auto *old_m = caller.get_access_meta(new_i)) {
        m.types = old_m->types;
        for (auto s : old_m->scope_in) {
          m.scope_in.push_back(remap(s));
        }
        for (auto s : old_m->not_in) {
          m.not_in.push_back(remap(s));
        }
      }
      if (!restricts.empty()) {
        const SetId set = prov.set_of(old_i->args[0]);
        const FVec<SetId> leaves =
            ptab.is_union(set)
                ? FVec<SetId>(ptab.parts(set).begin(), ptab.parts(set).end())
                : FVec<SetId>{set};
        for (const auto &r : restricts) {
          bool only = true;
          bool possible = false;
          for (auto leaf : leaves) {
            only = only && ptab.subset(leaf, r.object);
            possible = possible || (!ptab.disjoint(leaf, r.object) &&
                                    !(r.nocapture && leaf == T::Foreign));
          }
          if (only) {
            add(m.scope_in, r.scope);
          } else if (!possible) {
            add(m.not_in, r.scope);
          }
        }
      }
      caller.set_access_meta(new_i, std::move(m));
    }
  }
}
}  // namespace

fir::BasicBlock split_block(fir::Instr at_instr) {
  // auto *ctx = at_instr->get_parent()->get_parent()->ctx;
  fir::BasicBlock t_bb = at_instr->get_parent();
  fir::FunctionR t_func = t_bb->get_parent();

  fir::Builder bb = t_func.builder();
  fir::BasicBlock new_bb = bb.append_bb();

  size_t instr_id = 0;
  while (t_bb->instructions[instr_id] != at_instr) {
    instr_id++;
    ASSERT(instr_id < t_bb->instructions.size());
  }
  instr_id++;

  auto start_id = instr_id;
  for (; instr_id < t_bb->instructions.size(); instr_id++) {
    new_bb.push_instr(t_bb->instructions[instr_id]);
  }
  t_bb->instructions.erase(
      t_bb->instructions.begin() + static_cast<i64>(start_id),
      t_bb->instructions.end());
  return new_bb;
}

bool inline_call(fir::Instr call) {
  ASSERT(call->is(fir::InstrType::CallInstr));

  if (!call->args[0].is_constant()) {
    return false;
  }

  const u32 n_results = call->n_results();
  bool has_ret_value = n_results > 0;

  auto *ctx = call->get_parent()->get_parent()->ctx;
  fir::BasicBlock call_bb = call->get_parent();
  fir::FunctionR call_func = call_bb->get_parent();
  const auto called_func = call->args[0].as_constant()->as_func();
  if (called_func->is_decl()) {
    return false;
  }

  // TODO: impl to hndle this correctly
  TVec<fir::Instr> entry_allocas;
  for (auto instr : called_func->basic_blocks[0]->instructions) {
    if (instr->is(fir::InstrType::AllocaInstr)) {
      // only static onces for now
      if (instr->args[0].is_constant()) {
        entry_allocas.push_back(instr);
      } else {
        return false;
      }
    }
  }

  fir::ContextData::V2VMap subs;
  fir::ContextData::V2VMap bb_subs;
  TVec<fir::BasicBlock> new_bbs;
  for (size_t bb_id = 0; bb_id < called_func->n_bbs(); bb_id++) {
    // dont apply the subs here
    auto new_bb = ctx->copy(called_func->basic_blocks.at(bb_id), subs, false,
                            fir::MetaCopy::All, call_func.func);
    new_bbs.push_back(new_bb);
  }
  scope_inlined_accesses(ctx, *called_func.func, *call_func.func, new_bbs);
  if (has_ret_value) {
    for (auto bb : new_bbs) {
      if (bb->instructions.back()->is(fir::InstrType::ReturnInstr)) {
        if (!bb->instructions.back()->has_args()) {
          // TODO: why does this occur ?
          //  failure("Cant inline function with weird returns\n");
          fmt::println("{:cd}", *called_func.func);
          fmt::println("{:cd}", called_func->func_ty);
          TODO("hit weird");
          return false;
        }
      }
    }
  }

  auto end_bb = split_block(call);

  if (has_ret_value) {
    for (u32 i = 0; i < n_results; i++) {
      end_bb.add_arg(ctx->storage.insert_bb_arg(end_bb, call->result_type(i)));
    }
  }

  // make copy and then insert otherwise we get infinite loop when inlining
  // recursive call
  for (auto new_bb : new_bbs) {
    call_func->append_bbr(new_bb);
  }

  // we need to run subs afterwards since vlaues can be referenced before they
  // are defined if their bb comes later
  for (auto bb : new_bbs) {
    for (auto instr : bb->instructions) {
      instr.substitute(subs);
    }
  }
  auto new_entry = subs.at(fir::ValueR{called_func->basic_blocks[0]});

  fir::Builder bb = call_bb.builder_at_end();
  auto entry_branch = bb.build_branch(new_entry.as_bb());

  // add the args

  for (size_t arg_id = 1; arg_id < call->args.size(); arg_id++) {
    ASSERT(call->args[arg_id].is_valid(true));
    entry_branch.add_bb_arg(0, call->args[arg_id]);
  }

  // replace every return isntruction with a jump to the resulting bb
  // who gets an bb_arg that holds the value
  if (has_ret_value) {
    for (u32 i = 0; i < n_results; i++) {
      call.result(i).replace_all_uses(fir::ValueR{end_bb->args[i]});
    }
  }
  for (auto bb : new_bbs) {
    fir::Builder ret_bb = bb.builder_at_end();
    for (size_t instr_id = 0; instr_id < bb->instructions.size(); instr_id++) {
      if (bb->instructions[instr_id]->is(fir::InstrType::ReturnInstr)) {
        auto end_branch = ret_bb.build_branch(end_bb);
        if (has_ret_value) {
          ASSERT(bb->instructions[instr_id]->has_args());
          if (call->is_multi_result()) {
            ASSERT(bb->instructions[instr_id]->args.size() == n_results);
            for (auto a : bb->instructions[instr_id]->args) {
              end_branch.add_bb_arg(0, a);
            }
          } else if (bb->instructions[instr_id]->args.size() == 1) {
            end_branch.add_bb_arg(0, bb->instructions[instr_id]->get_arg(0));
          } else {
            auto old_ret = bb->instructions[instr_id];
            fir::Builder buh{end_branch};
            auto res_typ = old_ret.result_type();
            auto v = fir::ValueR{ctx->get_poisson_value(res_typ)};
            u32 index = 0;
            for (auto a : old_ret->args) {
              fir::ValueR args[1] = {
                  fir::ValueR{ctx->get_constant_int(index, 64)}};
              v = buh.build_insert_element(v, a, args, res_typ);
              index++;
            }
            end_branch.add_bb_arg(0, v);
          }
        }
        bb->remove_instr(instr_id, true);
      }
    }
  }

  if (!entry_allocas.empty()) {
    for (auto old_alloca : entry_allocas) {
      auto alloca = subs.at(old_alloca.result()).as_instr();
      auto entry = call_func->get_entry();
      fir::Builder buh{entry};
      auto new_alloca = buh.build_alloca(alloca->args[0]);
      alloca->replace_all_uses(new_alloca);
      alloca.destroy();
    }
  }

  call->remove_all_usages();
  call.clear_args();
  call.clear_bbs();
  call.destroy();

  // ASSERT(ctx->verify());
  // TODO("impl");
  return true;
}

}  // namespace foptim::optim
