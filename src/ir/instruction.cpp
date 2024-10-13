#include "ir/instruction.hpp"
#include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "utils/logging.hpp"

namespace foptim::fir {

void Instr::remove_from_parent() {
  auto *self = operator->();
  BasicBlock parent = self->get_parent();
  assert(parent.is_valid());
  for (size_t indx = 0; indx < parent->instructions.size(); indx++) {
    auto instr = parent->instructions[indx];
    if (get_raw_ptr() == instr.get_raw_ptr()) {
      parent->remove_instr(indx);
      return;
    }
  }
}

bool Instr::substitute(const FMap<ValueR, ValueR> &repl) {
  auto *self = operator->();
  const auto n_args = self->args.size();
  bool replaced = false;
  for (size_t i = 0; i < n_args; i++) {
    if (repl.contains(self->args[i])) {
      replace_arg(i, repl.at(self->args[i]));
      replaced = true;
    }
  }

  for (auto &bb : self->bbs) {
    const auto n_args = bb.args.size();
    for (size_t i = 0; i < n_args; i++) {
      if (repl.contains(bb.args[i])) {
        replace_arg(i, repl.at(bb.args[i]));
        replaced = true;
      }
    }
  }
  return replaced;
}

void Instr::clear_args() {
  auto *self = operator->();
  auto &args = self->args;
  for (size_t i = 0; i < args.size(); i++) {
    if (!args[i].is_constant()) {
      args[i].remove_usage(Use::norm(*this, i));
    }
  }
  args.clear();
}

void Instr::clear_bbs() {
  auto *self = operator->();
  auto &bbs = self->bbs;
  for (size_t i = 0; i < bbs.size(); i++) {
    // for (size_t arg_id = 0; arg_id < bbs[i].args.size(); arg_id++) {
    //   if (!bbs[i].args[arg_id].is_constant()) {
    //     bbs[i].args[arg_id].remove_usage(Use::bb_arg(*this, i, arg_id));
    //   }
    // }
    clear_bb_args(i);
    bbs[i].bb->remove_usage(Use::bb(*this, i));
  }
  bbs.clear();
}

void Instr::clear_bb_args(u16 indx) {
  auto *self = operator->();
  auto &bbs = self->bbs;
  for (size_t arg_id = 0; arg_id < bbs[indx].args.size(); arg_id++) {
    if (!bbs[indx].args[arg_id].is_constant()) {
      bbs[indx].args[arg_id].remove_usage(Use::bb_arg(*this, indx, arg_id));
    }
  }
  bbs[indx].args.clear();
}

TypeR Instr::get_type() const { return (this->operator->())->get_type(); }

ValueR Instr::replace_arg(u16 indx, ValueR new_val) {
  InstrData *self = operator->();
  ASSERT(indx < self->args.size());
  ValueR old_val = self->args[indx];
  self->args[indx] = new_val;

  new_val.add_usage(Use::norm(*this, indx));
  old_val.remove_usage(Use::norm(*this, indx));

  return old_val;
}

BasicBlock Instr::replace_bb(u16 indx, BasicBlock new_val, bool keepArgs) {
  InstrData *self = operator->();
  ASSERT(indx < self->bbs.size());
  BasicBlock old_val = self->bbs[indx].bb;
  self->bbs[indx].bb = new_val;

  new_val->add_usage(Use::bb(*this, indx));
  old_val->remove_usage(Use::bb(*this, indx));
  if (!keepArgs) {
    clear_bb_args(indx);
  }

  return old_val;
}

BasicBlock Instr::replace_bb(BasicBlock target, BasicBlock new_val,
                             bool keepArgs) {
  InstrData *self = operator->();
  u16 bb_indx = 0;
  for (auto &bb_ref : self->bbs) {
    if (bb_ref.bb == target) {
      return replace_bb(bb_indx, new_val, keepArgs);
    }
    bb_indx++;
  }
  utils::Debug << "In instruction " << *this << "\n";
  utils::Debug << "  Tried replacing bb '" << (void *)target.get_raw_ptr()
               << "'\n";
  utils::Debug << "  with new val '" << new_val << "'\n";
  utils::Debug << "BUT it does not reference this basic block\n";

  std::abort();
}

ValueR Instr::replace_bb_arg(BasicBlock target, u16 indx, ValueR new_val) {
  InstrData *self = operator->();
  u16 bb_indx = 0;
  for (auto &bb_ref : self->bbs) {
    if (bb_ref.bb == target) {
      return replace_bb_arg(bb_indx, indx, new_val);
    }
    bb_indx++;
  }
  utils::Debug << "In instruction " << *this << "\n";
  utils::Debug << "  Tried replacing bb arg '" << (void *)target.get_raw_ptr()
               << "'\n";
  utils::Debug << "  At index '" << indx << "'\n";
  utils::Debug << "  with new val '" << new_val << "'\n";
  utils::Debug << "BUT it does not reference this basic block\n";

  std::abort();
}

ValueR Instr::replace_bb_arg(u16 bb_id, u16 indx, ValueR new_val) {
  InstrData *self = operator->();
  auto &bb_ref = self->bbs[bb_id];
  if (bb_ref.args.size() <= indx) {
    bb_ref.args.resize(indx + 1);
  }
  ValueR old_val = bb_ref.args[indx];
  bb_ref.args[indx] = new_val;

  new_val.add_usage(Use::bb_arg(*this, bb_id, indx));
  if (!old_val.is_constant()) {
    old_val.remove_usage(Use::bb_arg(*this, bb_id, indx));
  }

  return old_val;
}

u16 Instr::add_arg(ValueR v) {
  InstrData *self = operator->();
  self->args.push_back(v);
  v.add_usage(Use::norm(*this, (u16)(self->args.size() - 1)));
  return self->args.size() - 1;
}

u16 Instr::add_bb_arg(u16 bb, ValueR v) {
  InstrData *self = operator->();
  self->bbs[bb].args.push_back(v);
  u16 arg_off = self->bbs[bb].args.size() - 1;
  v.add_usage(Use::bb_arg(*this, bb, arg_off));
  return arg_off;
}

u16 Instr::add_bb_arg(BasicBlock target, ValueR val) {
  InstrData *self = operator->();
  u16 bb_indx = 0;
  for (auto &bb_ref : self->bbs) {
    if (bb_ref.bb == target) {
      return add_bb_arg(bb_indx, val);
    }
    bb_indx++;
  }
  utils::Debug << "In instruction " << *this << "\n";
  utils::Debug << "  Tried adding bb arg '" << (void *)target.get_raw_ptr()
               << "'\n";
  utils::Debug << "  with new val '" << val << "'\n";
  utils::Debug << "BUT it does not reference this basic block\n";

  std::abort();
}

u16 Instr::add_bb(BasicBlock val) {
  InstrData *self = operator->();
  self->bbs.push_back({val, {}});
  val->add_usage(Use::bb(*this, self->bbs.size() - 1));
  return self->bbs.size() - 1;
}

} // namespace foptim::fir
