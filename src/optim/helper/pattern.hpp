#pragma once
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "utils/arena.hpp"
namespace foptim::optim::matcher {

namespace internal {
class Matcher {
 public:
  Matcher() = default;
  virtual ~Matcher() = default;
  virtual bool match(fir::ValueR v);
};

struct AnyConstMatcher : Matcher {
  bool match(fir::ValueR v) override {
    if (v.is_constant()) {
      return true;
    };
    return false;
  }
};

struct IntConstMatcher : Matcher {
  i128 exp;

  IntConstMatcher(i128 exp) : exp(exp) {}

  bool match(fir::ValueR v) override {
    if (v.is_constant()) {
      auto c = v.as_constant();
      if (c->is_int() && c->as_int() == exp) {
        return true;
      }
    };
    return false;
  }
};

struct AddMatcher : Matcher {
  Matcher* a;
  Matcher* b;

  AddMatcher(Matcher* a, Matcher* b) : a(a), b(b) {}

  bool match(fir::ValueR v) override {
    if (v.is_instr()) {
      auto c = v.as_instr();
      if (c->is(fir::BinaryInstrSubType::IntAdd) ||
          c->is(fir::BinaryInstrSubType::FloatAdd)) {
        if (a->match(v) && b->match(v)) {
          return true;
        }
      }
    };
    return false;
  }
};
}  // namespace internal
using namespace internal;

inline AnyConstMatcher* AnyConst() {
  auto* d = utils::TempAlloc<AnyConstMatcher>{}.allocate(1);
  new (d) AnyConstMatcher;
  return d;
}

inline IntConstMatcher* Int(i128 c) {
  auto* d = utils::TempAlloc<IntConstMatcher>{}.allocate(1);
  new (d) IntConstMatcher{c};
  return d;
}

inline AddMatcher* Add(Matcher* a, Matcher* b) {
  auto* d = utils::TempAlloc<AddMatcher>{}.allocate(1);
  new (d) AddMatcher{a, b};
  return d;
}

}  // namespace foptim::optim::matcher
