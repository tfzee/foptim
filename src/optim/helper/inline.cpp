#include "inline.hpp"

#include "ir/basic_block.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "utils/tracy.hpp"

namespace foptim::optim {

using namespace fir;

fir::BasicBlock split_block(fir::Instr at_instr) {
  // auto *ctx = at_instr->get_parent()->get_parent()->ctx;
  BasicBlock t_bb = at_instr->get_parent();
  FunctionR t_func = t_bb->get_parent();

  Builder bb = t_func.builder();
  BasicBlock new_bb = bb.append_bb();

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
  t_bb->instructions.erase(t_bb->instructions.begin() + (i64)start_id,
                           t_bb->instructions.end());
  return new_bb;
}

bool inline_call(fir::Instr call) {
  ASSERT(call->is(InstrType::CallInstr));

  if (!call->args[0].is_constant()) {
    return false;
  }

  bool has_ret_value = call->has_result();

  auto *ctx = call->get_parent()->get_parent()->ctx;
  BasicBlock call_bb = call->get_parent();
  FunctionR call_func = call_bb->get_parent();
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

  ContextData::V2VMap subs;
  ContextData::V2VMap bb_subs;
  TVec<BasicBlock> new_bbs;
  for (size_t bb_id = 0; bb_id < called_func->n_bbs(); bb_id++) {
    // dont apply the subs here
    auto new_bb = ctx->copy(called_func->basic_blocks.at(bb_id), subs, false);
    new_bbs.push_back(new_bb);
  }
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
    auto new_arg = ctx->storage.insert_bb_arg(end_bb, call->get_type());
    end_bb.add_arg(new_arg);
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
  auto new_entry = subs.at(ValueR{called_func->basic_blocks[0]});

  Builder bb = call_bb.builder_at_end();
  auto entry_branch = bb.build_branch(new_entry.as_bb());

  // add the args

  for (size_t arg_id = 1; arg_id < call->args.size(); arg_id++) {
    ASSERT(call->args[arg_id].is_valid(true));
    entry_branch.add_bb_arg(0, call->args[arg_id]);
  }

  // replace every return isntruction with a jump to the resulting bb
  // who gets an bb_arg that holds the value
  if (has_ret_value) {
    call->replace_all_uses(ValueR{end_bb->args[0]});
  }
  for (auto bb : new_bbs) {
    Builder ret_bb = bb.builder_at_end();
    for (size_t instr_id = 0; instr_id < bb->instructions.size(); instr_id++) {
      if (bb->instructions[instr_id]->is(fir::InstrType::ReturnInstr)) {
        auto end_branch = ret_bb.build_branch(end_bb);
        if (has_ret_value) {
          ASSERT(bb->instructions[instr_id]->has_args());
          if (bb->instructions[instr_id]->args.size() == 1) {
            end_branch.add_bb_arg(0, bb->instructions[instr_id]->get_arg(0));
          } else {
            auto old_ret = bb->instructions[instr_id];
            fir::Builder buh{end_branch};
            auto res_typ = old_ret.get_type();
            auto v = fir::ValueR{ctx->get_poisson_value(res_typ)};
            u32 index = 0;
            for (auto a : old_ret->args) {
              fir::ValueR args[1] = {
                  fir::ValueR{ctx->get_constant_int(index, 64)}};
              v = buh.build_insert_value(v, a, args, res_typ);
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
      auto alloca = subs.at(fir::ValueR{old_alloca}).as_instr();
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
