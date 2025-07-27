#include "value.hpp"
#include "instruction_data.hpp"
#include "ir/basic_block.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/use.hpp"
#include "utils/logging.hpp"
#include <fmt/color.h>
#include <fmt/format.h>

namespace foptim::fir {

bool ValueR::is_valid(bool check_refs) const {
  switch (ty) {
  case ValueType::InvalidValue:
    return false;
  case ValueType::Instr:
    return instr.is_valid();
  case ValueType::BBArg:
    return bb_arg.is_valid();
  case ValueType::ConstantValueR:
    return const_val.is_valid() && (!check_refs || const_val->is_valid());
  case ValueType::BasicBlock:
    return bb.is_valid();
  }
}

bool ValueR::operator==(const ValueR &other) const { return this->eql(other); }

bool ValueR::eql(const ValueR &other) const {
  if (ty != other.ty) {
    return false;
  }
  switch (ty) {
  case ValueType::InvalidValue:
    return true;
  case ValueType::Instr:
    return (void *)instr.get_raw_ptr() == (void *)other.instr.get_raw_ptr();
  case ValueType::BasicBlock:
    return (void *)bb.get_raw_ptr() == (void *)other.bb.get_raw_ptr();
  case ValueType::BBArg:
    return (void *)bb_arg.get_raw_ptr() == (void *)other.bb_arg.get_raw_ptr();
  case ValueType::ConstantValueR:
    return const_val->eql(*other.const_val.get_raw_ptr());
  }
  // fmt::println("{} and {}", *this, other);
  // UNREACH();
}

TypeR ValueR::get_type() const {
  switch (ty) {
  case ValueType::InvalidValue:
    return TypeR(TypeR::invalid());
  case ValueType::Instr:
    return instr.get_type();
  case ValueType::BasicBlock:
    return bb->get_type();
  case ValueType::BBArg:
    return bb_arg->get_type();
  case ValueType::ConstantValueR:
    return const_val->get_type();
  }
}

void ValueR::add_usage(Use u) {
  switch (ty) {
  case ValueType::Instr:
    return instr->add_usage(u);
  case ValueType::BBArg:
    return bb_arg->add_usage(u);
  case ValueType::BasicBlock:
    return bb->add_usage(u);
  case ValueType::ConstantValueR:
    return const_val->add_usage(u);
  case ValueType::InvalidValue:
    return;
  }
}

IRVec<Use> *ValueR::get_uses() {
  switch (ty) {
  case ValueType::Instr:
    return &instr->uses;
  case ValueType::BBArg:
    return &bb_arg->uses;
  case ValueType::BasicBlock:
    return &bb->uses;
  case ValueType::ConstantValueR:
    return const_val->get_uses();
  case ValueType::InvalidValue:
    return nullptr;
  }
}

const IRVec<Use> *ValueR::get_uses() const {
  switch (ty) {
  case ValueType::Instr:
    return &instr->uses;
  case ValueType::BBArg:
    return &bb_arg->uses;
  case ValueType::BasicBlock:
    return &bb->uses;
  case ValueType::ConstantValueR:
    return const_val->get_uses();
  case ValueType::InvalidValue:
    return nullptr;
  }
}

size_t ValueR::get_n_uses() const {
  switch (ty) {
  case ValueType::Instr:
    return instr->get_n_uses();
  case ValueType::BBArg:
    return bb_arg->get_n_uses();
  case ValueType::BasicBlock:
    return bb->get_n_uses();
  case ValueType::ConstantValueR:
    return const_val->get_n_uses();
  case ValueType::InvalidValue:
    return 0;
  }
}

void ValueR::remove_usage(Use u, bool verify) {
  switch (ty) {
  case ValueType::Instr:
    return instr->remove_usage(u, verify);
  case ValueType::BBArg:
    return bb_arg->remove_usage(u, verify);
  case ValueType::BasicBlock:
    return bb->remove_usage(u, verify);
  case ValueType::ConstantValueR:
    return const_val->remove_usage(u, verify);
  case ValueType::InvalidValue:
    return;
  }
}

void ValueR::replace_all_uses(ValueR new_value) {
  switch (ty) {
  case ValueType::Instr:
    return instr->replace_all_uses(new_value);
  case ValueType::BBArg:
    return bb_arg->replace_all_uses(new_value);
  case ValueType::BasicBlock:
    return bb->replace_all_uses(new_value);
  case ValueType::ConstantValueR:
    return const_val->replace_all_uses(new_value);
  case ValueType::InvalidValue:
    return;
  }
}

} // namespace foptim::fir

fmt::appender
fmt::formatter<foptim::fir::ValueR>::format(foptim::fir::ValueR const &k,
                                            format_context &ctx) const {
  auto col1 = color ? color_value : text_style{};
  auto col2 = color ? color_value2 : text_style{};
  auto colbb = color ? color_bb : text_style{};
  switch (k.ty) {
  case foptim::fir::ValueType::InvalidValue:
    return fmt::format_to(ctx.out(), "INVALID");
  case foptim::fir::ValueType::Instr:
    return fmt::format_to(ctx.out(), col1, "{:p}",
                          (void *)k.instr.get_raw_ptr());
  case foptim::fir::ValueType::BasicBlock:
    return fmt::format_to(ctx.out(), colbb, "{:p}", (void *)k.bb.get_raw_ptr());
  case foptim::fir::ValueType::BBArg:
    if (debug && color) {
      return fmt::format_to(ctx.out(), col2, "{:cd}", k.bb_arg);
    }
    if (color) {
      return fmt::format_to(ctx.out(), col2, "{:c}", k.bb_arg);
    }
    if (debug) {
      return fmt::format_to(ctx.out(), col2, "{:d}", k.bb_arg);
    }
    return fmt::format_to(ctx.out(), col2, "{}", k.bb_arg);

  case foptim::fir::ValueType::ConstantValueR:
    if (debug && color) {
      return fmt::format_to(ctx.out(), "{:cd}", k.const_val);
    } else if (color) {
      return fmt::format_to(ctx.out(), "{:c}", k.const_val);
    } else if (debug) {
      return fmt::format_to(ctx.out(), "{:d}", k.const_val);
    } else {
      return fmt::format_to(ctx.out(), "{}", k.const_val);
    }
  }
}
