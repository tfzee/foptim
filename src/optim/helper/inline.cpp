#include "inline.hpp"
#include "ir/basic_block.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"

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
  t_bb->instructions.erase(t_bb->instructions.begin() + start_id,
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

  auto end_bb = split_block(call);

  if (has_ret_value) {
    auto new_arg = ctx->storage.insert_bb_arg(end_bb, call->get_type());
    end_bb.add_arg(new_arg);
  }

  ContextData::V2VMap subs;
  ContextData::V2VMap bb_subs;
  TVec<BasicBlock> new_bbs;
  for (size_t bb_id = 0; bb_id < called_func->n_bbs(); bb_id++) {
    // dont apply the subs here
    auto new_bb = ctx->copy(called_func->basic_blocks.at(bb_id), subs, false);
    new_bbs.push_back(new_bb);
    call_func->append_bbr(new_bb);
  }

  // utils::Debug << "###################################\n";
  // utils::Debug << call_func << "\n";
  // utils::Debug << "+++++++++++++++++++++++++++++++++++\n";
  // utils::Debug << called_func << "\n";
  // utils::Debug << "###################################\n";

  // we need to run subs afterwards since vlaues can be referenced before they
  // are defined
  //   if their bb comes later
  for (auto bb : new_bbs) {
    for (auto instr : bb->instructions) {
      instr.substitute(subs);
    }
  }
  auto new_entry = subs.at(ValueR{called_func->basic_blocks[0]});

  Builder bb = call_bb.builder_at_end();
  auto entry_branch = bb.build_branch(new_entry.as_bb());

  // add the args
  for (auto arg : call->args) {
    entry_branch.add_bb_arg(0, arg);
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
          end_branch.add_bb_arg(0, bb->instructions[instr_id]->get_arg(0));
        }
        bb->remove_instr(instr_id);
      }
    }
  }

  call->remove_all_usages();
  call.clear_args();
  call.clear_bbs();
  call.remove_from_parent();
  // utils::Debug << "###################################\n";
  // utils::Debug << call_func << "\n";
  // utils::Debug << "###################################\n";

  // ASSERT(ctx->verify());
  // TODO("impl");
  return true;
}

} // namespace foptim::optim
