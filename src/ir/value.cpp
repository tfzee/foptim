#include "value.hpp"
#include "instruction_data.hpp"
#include "ir/basic_block.hpp"
#include "ir/constant_value.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/use.hpp"
#include <typeinfo>
#include <variant>

namespace foptim::fir {

bool ValueR::is_valid(bool check_refs) const {
  if (std::holds_alternative<InvalidValue>(origin)) {
    return false;
  }
  if (check_refs) {
    return std::visit(
        [](auto &&v) {
          if constexpr (typeid(v) == typeid(ConstantValueR) ||
                        typeid(v) == typeid(Instr)) {
            return v.is_valid();
          }
          // TOOD: could also check bb arg ref
          return true;
        },
        origin);
  }
  return true;
}

bool ValueR::eql(const ValueR &other) const {
  if (origin.index() != other.origin.index()) {
    return false;
  }

  if (const Instr *s = std::get_if<Instr>(&origin)) {
    const Instr *o = std::get_if<Instr>(&other.origin);
    return (void *)s->get_raw_ptr() == (void *)o->get_raw_ptr();
  }
  if (const auto *s = std::get_if<BasicBlock>(&origin)) {
    const auto *o = std::get_if<BasicBlock>(&other.origin);
    return (void *)s->get_raw_ptr() == (void *)o->get_raw_ptr();
  }
  if (const BBArgument *s = std::get_if<BBArgument>(&origin)) {
    const BBArgument *o = std::get_if<BBArgument>(&other.origin);
    return (void *)s->get_raw_ptr() == (void *)o->get_raw_ptr();
  }
  if (const ConstantValueR *s = std::get_if<ConstantValueR>(&origin)) {
    const ConstantValueR *o = std::get_if<ConstantValueR>(&other.origin);
    return (*s)->eql(*o->operator->());
  }
  if (std::get_if<InvalidValue>(&origin) != nullptr) {
    return true;
  }

  utils::Debug << this << " and " << other << "\n";
  UNREACH();
}

TypeR ValueR::get_type() const {
  return std::visit(
      [](auto &&v) {
        if constexpr (typeid(v) == typeid(ConstantValue)) {
          return v.get_type();
        } else if constexpr (typeid(v) == typeid(InvalidValue)) {
          std::abort();
          return TypeR(TypeR::invalid());
        } else {
          return v->get_type();
        }
      },
      origin);
}

void ValueR::add_usage(Use u) {
  if (auto *i = std::get_if<Instr>(&origin)) {
    (*i)->add_usage(u);
  } else if (auto *i = std::get_if<BBArgument>(&origin)) {
    (*i)->add_usage(u);
  }
  // else if (std::get_if<ConstantValueR>(&origin) != nullptr) {
  // } else if (std::get_if<InvalidValue>(&origin) != nullptr) {
  //   // ASSERT(false);
  // }
}

IRVec<Use> *ValueR::get_uses() {
  if (auto *i = std::get_if<Instr>(&origin)) {
    return &(*i)->uses;
  }
  if (auto *i = std::get_if<BBArgument>(&origin)) {
    return &(*i)->uses;
  }
  return nullptr;
}

const IRVec<Use> *ValueR::get_uses() const {
  if (const auto *i = std::get_if<Instr>(&origin)) {
    return &(*i)->uses;
  }
  if (const auto *i = std::get_if<BBArgument>(&origin)) {
    return &(*i)->uses;
  }
  return nullptr;
}

size_t ValueR::get_n_uses() const {
  if (const auto *i = std::get_if<Instr>(&origin)) {
    return (*i)->get_n_uses();
  }
  if (const auto *i = std::get_if<BBArgument>(&origin)) {
    return (*i)->get_n_uses();
  }
  return 0;
}

void ValueR::remove_usage(Use u, bool verify) {
  if (auto *i = std::get_if<Instr>(&origin)) {
    (*i)->remove_usage(u, verify);
    return;
  }
  if (auto *i = std::get_if<BBArgument>(&origin)) {
    (*i)->remove_usage(u, verify);
    return;
  }
  if (std::get_if<ConstantValueR>(&origin) != nullptr) {
    return;
  }
  // ASSERT(false);
}

void ValueR::replace_all_uses(ValueR new_value) {
  if (auto *i = std::get_if<Instr>(&origin)) {
    (*i)->replace_all_uses(new_value);
    return;
  }
  if (auto *i = std::get_if<BBArgument>(&origin)) {
    (*i)->replace_all_uses(new_value);
    return;
  }
  ASSERT(false);
}

} // namespace foptim::fir
