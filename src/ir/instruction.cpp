#include <fmt/color.h>

#include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "ir/value.hpp"
#include "utils/logging.hpp"
#include "utils/stable_vec_ref.hpp"

namespace foptim::fir {

void Instr::destroy() {
  ASSERT(is_valid());
  this->clear_args();
  for (size_t bb_id = 0; bb_id < (*this)->bbs.size(); bb_id++) {
    this->clear_bb_args(bb_id);
  }
  this->clear_bbs();
  remove_from_parent();
  _invalidate();
}

void Instr::remove_from_parent() {
  auto *self = operator->();
  BasicBlock parent = self->get_parent();
  assert(parent.is_valid());
  for (size_t indx = 0; indx < parent->instructions.size(); indx++) {
    auto instr = parent->instructions[indx];
    if ((void *)get_raw_ptr() == (void *)instr.get_raw_ptr()) {
      parent->remove_instr(indx, false);
      return;
    }
  }
}

namespace {

template <class T>
bool substitute_impl(Instr &t, const T &repl) {
  auto *self = t.operator->();
  const auto n_args = self->args.size();
  bool replaced = false;
  for (size_t i = 0; i < n_args; i++) {
    if (repl.contains(self->args[i])) {
      t.replace_arg(i, repl.at(self->args[i]), false);
      replaced = true;
    }
  }

  for (size_t bb_id = 0; bb_id < self->bbs.size(); bb_id++) {
    auto &bb = self->bbs[bb_id];
    if (repl.contains(ValueR{bb.bb})) {
      t.replace_bb(bb_id, repl.at(ValueR{bb.bb}).as_bb(), true, false);
    }
    const auto n_args = bb.args.size();
    for (size_t i = 0; i < n_args; i++) {
      if (repl.contains(bb.args[i])) {
        t.replace_bb_arg(bb_id, i, repl.at(bb.args[i]), false);
        replaced = true;
      }
    }
  }
  return replaced;
}

}  // namespace

bool Instr::substitute(const FMap<ValueR, ValueR> &repl) {
  return substitute_impl(*this, repl);
}
bool Instr::substitute(const TMap<ValueR, ValueR> &repl) {
  return substitute_impl(*this, repl);
}

void Instr::clear_args() {
  auto *self = operator->();
  auto &args = self->args;
  for (size_t i = 0; i < args.size(); i++) {
    args[i].remove_usage(Use::norm(*this, i));
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
    bbs[i].bb->remove_usage(Use::bb(*this, i), true);
  }
  bbs.clear();
}

void Instr::clear_bb_args(u16 indx) {
  auto *self = operator->();
  auto &bbs = self->bbs;
  for (size_t arg_id = 0; arg_id < bbs[indx].args.size(); arg_id++) {
    bbs[indx].args[arg_id].remove_usage(Use::bb_arg(*this, indx, arg_id));
  }
  bbs[indx].args.clear();
}

TypeR Instr::get_type() const { return (this->operator->())->get_type(); }

//@returns: the bb id in reference to which index it has in the targets of this
// instruction
u16 Instr::get_bb_id(BasicBlock target) const {
  const InstrData *self = operator->();
  u16 bb_indx = 0;
  for (const auto &bb_ref : self->bbs) {
    if (bb_ref.bb == target) {
      return bb_indx;
    }
    bb_indx++;
  }
  fmt::println("In instruction: {}", *this);
  fmt::println("Tried to get bb arg: {}", (void *)target.get_raw_ptr());
  fmt::println("BUT it does not reference this basic block");

  std::abort();
}

ValueR Instr::replace_arg(u16 indx, ValueR new_val, bool verify) {
  InstrData *self = operator->();
  if (indx >= self->args.size()) {
    fmt::println("Tried replacing arg {} but theres only {} Args", indx,
                 self->args.size());
    fmt::println("Instr: {}", *this);
    std::abort();
  }
  ValueR old_val = self->args[indx];
  if (old_val == new_val) {
    return old_val;
  }
  self->args[indx] = new_val;

  old_val.remove_usage(Use::norm(*this, indx), verify);
  new_val.add_usage(Use::norm(*this, indx));

  return old_val;
}

BasicBlock Instr::replace_bb(u16 indx, BasicBlock new_val, bool keepArgs,
                             bool verify) {
  InstrData *self = operator->();
  ASSERT(indx < self->bbs.size());
  BasicBlock old_val = self->bbs[indx].bb;
  if (old_val == new_val) {
    return old_val;
  }
  self->bbs[indx].bb = new_val;

  old_val->remove_usage(Use::bb(*this, indx), verify);
  new_val->add_usage(Use::bb(*this, indx));
  if (!keepArgs) {
    clear_bb_args(indx);
  }

  return old_val;
}

BasicBlock Instr::replace_bb(BasicBlock target, BasicBlock new_val,
                             bool keepArgs) {
  return replace_bb(get_bb_id(target), new_val, keepArgs);
}

ValueR Instr::replace_bb_arg(BasicBlock target, u16 indx, ValueR new_val) {
  return replace_bb_arg(get_bb_id(target), indx, new_val);
}

ValueR Instr::replace_bb_arg(u16 bb_id, u16 indx, ValueR new_val, bool verify) {
  InstrData *self = operator->();
  auto &bb_ref = self->bbs[bb_id];
  if (bb_ref.args.size() <= indx) {
    bb_ref.args.resize(indx + 1);
  } else if (bb_ref.args[indx] == new_val) {
    return bb_ref.args[indx];
  }
  ValueR old_val = bb_ref.args[indx];
  bb_ref.args[indx] = new_val;

  old_val.remove_usage(Use::bb_arg(*this, bb_id, indx), verify);
  new_val.add_usage(Use::bb_arg(*this, bb_id, indx));
  // if (!old_val.is_constant()) {
  // }

  return old_val;
}

void Instr::remove_arg(u16 indx, bool verify) {
  InstrData *self = operator->();
  auto &args = self->args;
  for (size_t i = indx + 1; i < args.size(); i++) {
    args[i].remove_usage(Use::norm(*this, i), verify);
    args[i].add_usage(Use::norm(*this, i - 1));
  }

  args[indx].remove_usage(Use::norm(*this, indx), verify);
  args.erase(args.begin() + indx);
}

void Instr::remove_bb(u16 indx, bool verify) {
  InstrData *self = operator->();
  auto &bbs = self->bbs;
  for (size_t i = indx + 1; i < bbs.size(); i++) {
    bbs[i].bb->remove_usage(Use::bb(*this, i), verify);
    bbs[i].bb->add_usage(Use::bb(*this, i - 1));
    for (size_t a = 0; a < bbs[i].args.size(); a++) {
      bbs[i].args[a].remove_usage(Use::bb_arg(*this, i, a), verify);
      bbs[i].args[a].add_usage(Use::bb_arg(*this, i - 1, a));
    }
  }

  bbs[indx].bb->remove_usage(Use::bb(*this, indx), verify);
  for (size_t a = 0; a < bbs[indx].args.size(); a++) {
    bbs[indx].args[a].remove_usage(Use::bb_arg(*this, indx, a), verify);
  }
  bbs.erase(bbs.begin() + indx);
}

void Instr::remove_bb_arg(u16 bb_id, u16 indx1, bool verify) {
  InstrData *self = operator->();
  auto &bb_args = self->bbs[bb_id].args;
  // fix all the args after
  for (size_t i = indx1 + 1; i < self->bbs[bb_id].args.size(); i++) {
    bb_args[i].remove_usage(Use::bb_arg(*this, bb_id, i), verify);
    bb_args[i].add_usage(Use::bb_arg(*this, bb_id, i - 1));
  }

  bb_args[indx1].remove_usage(Use::bb_arg(*this, bb_id, indx1), verify);
  bb_args.erase(self->bbs[bb_id].args.begin() + indx1);
}

// void Instr::swap_bb_args(u16 bb_id, u16 indx1, u16 indx2, bool verify) {
//   InstrData *self = operator->();
//   auto &bb_ref = self->bbs[bb_id];
//   auto n_args = bb_ref.args.size();
//   ASSERT(n_args > indx1);
//   ASSERT(n_args > indx2);

//   ValueR old_val1 = bb_ref.args[indx1];
//   ValueR old_val2 = bb_ref.args[indx2];
//   bb_ref.args[indx1] = old_val2;
//   bb_ref.args[indx2] = old_val1;

//   old_val1.remove_usage(Use::bb_arg(*this, bb_id, indx1), verify);
//   old_val1.add_usage(Use::bb_arg(*this, bb_id, indx2));

//   old_val2.remove_usage(Use::bb_arg(*this, bb_id, indx2), verify);
//   old_val2.add_usage(Use::bb_arg(*this, bb_id, indx1));
//   // if (!old_val.is_constant()) {
//   // }
// }
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
  return add_bb_arg(get_bb_id(target), val);
}

u16 Instr::add_bb(BasicBlock val) {
  InstrData *self = operator->();
  self->bbs.push_back({val, {}});
  val->add_usage(Use::bb(*this, self->bbs.size() - 1));
  return self->bbs.size() - 1;
}

}  // namespace foptim::fir

fmt::appender fmt::formatter<foptim::fir::BBRefWithArgs>::format(
    foptim::fir::BBRefWithArgs const &bb_with_args, format_context &ctx) const {
  auto app = ctx.out();
  if (color) {
    app = fmt::format_to(app, color_bb, "{:p}",
                         (void *)bb_with_args.bb.get_raw_ptr());
  } else {
    app = fmt::format_to(app, "{:p}", (void *)bb_with_args.bb.get_raw_ptr());
  }
  app = fmt::format_to(app, "(");
  if (!bb_with_args.args.empty()) {
    if (color) {
      app = fmt::format_to(app, "{:c}", bb_with_args.args[0]);
    } else {
      app = fmt::format_to(app, "{}", bb_with_args.args[0]);
    }
    for (size_t i = 1; i < bb_with_args.args.size(); i++) {
      if (color) {
        app = fmt::format_to(app, ", {:c}", bb_with_args.args[i]);
      } else {
        app = fmt::format_to(app, ", {}", bb_with_args.args[i]);
      }
    }
  }
  app = fmt::format_to(app, ")");
  return app;
}

fmt::appender fmt::formatter<foptim::fir::Instr>::format(
    foptim::fir::Instr const &instr, format_context &ctx) const {
  auto app = ctx.out();
  if (!instr.is_valid()) {
    return fmt::format_to(app, "INVALID\n");
  }

  if (color) {
    app = fmt::format_to(app, "{:p}: {:c} = {}",
                         fmt::styled((void *)instr.get_raw_ptr(), color_value),
                         instr->get_type(), instr->get_name());
  } else {
    app = fmt::format_to(app, "{:p}: {} = {}", (void *)instr.get_raw_ptr(),
                         instr->get_type(), instr->get_name());
  }
  const auto &bb_args = instr->get_bb_args();
  if (bb_args.size() > 0) {
    if (debug && color) {
      app = fmt::format_to(app, "<{:cd}", bb_args[0]);
    } else if (debug) {
      app = fmt::format_to(app, "<{:d}", bb_args[0]);
    } else if (color) {
      app = fmt::format_to(app, "<{:c}", bb_args[0]);
    } else {
      app = fmt::format_to(app, "<{}", bb_args[0]);
    }
    for (size_t i = 1; i < bb_args.size(); i++) {
      if (debug && color) {
        app = fmt::format_to(app, ", {:cd}", bb_args[i]);
      } else if (debug) {
        app = fmt::format_to(app, ", {:d}", bb_args[i]);
      } else if (color) {
        app = fmt::format_to(app, ", {:c}", bb_args[i]);
      } else {
        app = fmt::format_to(app, ", {}", bb_args[i]);
      }
    }
    app = fmt::format_to(app, ">");
  }

  app = fmt::format_to(app, "(");
  const auto &args = instr->get_args();
  if (args.size() > 0) {
    if (debug && color) {
      app = fmt::format_to(app, "{:dc}", args[0]);
    } else if (debug) {
      app = fmt::format_to(app, "{:d}", args[0]);
    } else if (color) {
      app = fmt::format_to(app, "{:c}", args[0]);
    } else {
      app = fmt::format_to(app, "{}", args[0]);
    }
    for (size_t i = 1; i < args.size(); i++) {
      if (debug && color) {
        app = fmt::format_to(app, ", {:dc}", args[i]);
      } else if (debug) {
        app = fmt::format_to(app, ", {:d}", args[i]);
      } else if (color) {
        app = fmt::format_to(app, ", {:c}", args[i]);
      } else {
        app = fmt::format_to(app, ", {}", args[i]);
      }
    }
  }
  app = fmt::format_to(app, "){{");

  if (instr->extra_type.is_valid()) {
    app = fmt::format_to(app, "extTy: {}; ", instr->extra_type);
  }
  if (instr->NSW) {
    app = fmt::format_to(app, "NSW; ");
  }
  if (instr->NUW) {
    app = fmt::format_to(app, "NUW; ");
  }
  if (instr->Atomic) {
    app = fmt::format_to(app, "ATOMIC; ");
  }
  if (instr->Volatile) {
    app = fmt::format_to(app, "VOLATILE; ");
  }
  // fmt::println("TODO print attribs");
  // const auto &attribs = instr->get_attribs();
  // for (auto [key, value] : attribs) {
  //   app = fmt::format_to(app, "{}{}, ", key.c_str(), value);
  // }
  app = fmt::format_to(app, "}}");
  if (debug) {
    app = fmt::format_to(app, color ? color_debug : text_style{}, " NUses:{}",
                         instr->get_n_uses());
  }
  // app = fmt::format_to(app, "\n");
  return app;
}
