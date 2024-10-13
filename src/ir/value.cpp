#include "value.hpp"
#include "instruction_data.hpp"
#include "ir/basic_block.hpp"
#include "ir/constant_value.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/use.hpp"
#include <typeinfo>
#include <variant>

namespace foptim::fir {

TypeR BBArgumentR::get_type() const { return bb->args[arg].type; }

bool ValueR::eql(const ValueR &other) const {
  if (origin.index() != other.origin.index()) {
    return false;
  }

  if (const Instr *s = std::get_if<Instr>(&origin)) {
    const Instr *o = std::get_if<Instr>(&other.origin);
    return s->get_raw_ptr() == o->get_raw_ptr();
  } else if (const BBArgumentR *s = std::get_if<BBArgumentR>(&origin)) {
    const BBArgumentR *o = std::get_if<BBArgumentR>(&other.origin);
    return *s == *o;
  } else if (const ConstantValueR *s = std::get_if<ConstantValueR>(&origin)) {
    const ConstantValueR *o = std::get_if<ConstantValueR>(&other.origin);
    return (*s)->eql(*o->operator->());
  } else {
    return true;
  }
}

TypeR ValueR::get_type() const {
  return std::visit(
      [](auto &&v) {
        if constexpr (typeid(v) == typeid(ConstantValue) ||
                      typeid(v) == typeid(BBArgumentR)) {
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
  if (auto i = std::get_if<Instr>(&origin)) {
    (*i)->add_usage(u);
  } else if (auto *i = std::get_if<BBArgumentR>(&origin)) {
    (i->bb->args[i->arg]).add_usage(u);
  } else if (std::get_if<ConstantValueR>(&origin)) {
  } else if (std::get_if<InvalidValue>(&origin)) {
    // ASSERT(false);
  }
}

FVec<Use> *ValueR::get_uses() {
  if (auto i = std::get_if<Instr>(&origin)) {
    return &(*i)->uses;
  } else if (auto *i = std::get_if<BBArgumentR>(&origin)) {
    return &(i->bb->args[i->arg]).uses;
  } else {
    return nullptr;
  }
}

const FVec<Use> *ValueR::get_uses() const {
  if (auto i = std::get_if<Instr>(&origin)) {
    return &(*i)->uses;
  } else if (auto *i = std::get_if<BBArgumentR>(&origin)) {
    return &(i->bb->args[i->arg]).uses;
  } else {
    return nullptr;
  }
}

size_t ValueR::get_n_uses() const {
  if (auto i = std::get_if<Instr>(&origin)) {
    return (*i)->get_n_uses();
  } else if (auto *i = std::get_if<BBArgumentR>(&origin)) {
    return (i->bb->args[i->arg]).get_n_uses();
  } else {
    return 0;
  }
}

void ValueR::remove_usage(Use u) {
  if (auto i = std::get_if<Instr>(&origin)) {
    (*i)->remove_usage(u);
    return;
  } else if (auto *i = std::get_if<BBArgumentR>(&origin)) {
    (i->bb->args[i->arg]).remove_usage(u);
    return;
  } else if (std::get_if<ConstantValueR>(&origin)) {
    return;
  }
  // ASSERT(false);
}

void ValueR::replace_all_uses(ValueR new_value) {
  if (auto i = std::get_if<Instr>(&origin)) {
    (*i)->replace_all_uses(new_value);
    return;
  } else if (auto *i = std::get_if<BBArgumentR>(&origin)) {
    (i->bb->args[i->arg]).replace_all_uses(new_value);
    return;
  }
  ASSERT(false);
}

} // namespace foptim::fir
