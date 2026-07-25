#include "use.hpp"

#include <fmt/core.h>

#include "ir/instruction_data.hpp"
#include "value.hpp"

namespace foptim::fir {

TypeR Use::get_type() {
  switch (type) {
  case UseType::NormalArg:
    return user->args[argId].get_type();
  case UseType::BB:
    ASSERT(false);
  case UseType::BBArg:
    return user->bbs[argId].args[bbArgId].get_type();
  }
}

ValueR Use::get_value() {
  switch (type) {
  case UseType::NormalArg:
    return user->args[argId];
  case UseType::BB:
    ASSERT(false);
  case UseType::BBArg:
    return user->bbs[argId].args[bbArgId];
  }
}

void Use::replace_use(ValueR new_value) {
  switch (type) {
  case UseType::NormalArg:
    user.replace_arg(argId, new_value);
    break;
  case UseType::BBArg:
    user.replace_bb_arg(argId, bbArgId, new_value);
    break;
  case UseType::BB:
    user.replace_bb(argId, new_value.as_bb());
    break;
  }
}

void Used::replace_uses_outside_block(fir::BasicBlock bb, ValueR new_value) {
  // USE TVEC???
  TVec<Use> uses_copy{uses.begin(), uses.end()};
  for (Use &u : uses_copy) {
    if (u.user->get_parent() != bb) {
      u.replace_use(new_value);
    }
  }
}

void Used::replace_all_uses(ValueR new_value) {
  // USE TVEC???
  TVec<Use> uses_copy{uses.begin(), uses.end()};
  // auto uses_copy = uses;
  for (Use &u : uses_copy) {
    u.replace_use(new_value);
  }
}

void Used::remove_usage(const Use &u, bool verify) {
  for (size_t i = 0; i < uses.size(); i++) {
    if (uses[i].argId == u.argId && uses[i].bbArgId == u.bbArgId &&
        uses[i].type == u.type && uses[i].user == u.user) {
      uses.erase(uses.begin() + i);
      return;
    }
  }

  if (verify) {
    fmt::println(">NUSES: {}", uses.size());
    for (auto u : uses) {
      fmt::println("> {}", u);
    }
    fmt::println("USE: {}", u);
    fmt::println("USER: {}", u.user);
    ASSERT_M(false, "Failed to find usage that was to be removed");
  }
}

void Used::remove_all_usages() { replace_all_uses(ValueR()); }

void LockedUsed::replace_uses_outside_block(fir::BasicBlock bb,
                                            ValueR new_value) {
  TVec<fir::Use> uses_copy;
  {
    auto us = _uses.scoped_lock();
    uses_copy.reserve(us->uses.size());
    for (auto u : us->uses) {
      if (u.user->get_parent() != bb) {
        uses_copy.push_back(u);
      }
    }
  }
  for (Use &u : uses_copy) {
    ASSERT(u.user->get_parent() != bb);
    u.replace_use(new_value);
  }
  {
    auto us = _uses.scoped_lock();
    us->uses.clear();
  }
}
// Mutex<void *> FuncLockedUsed::lock = {};
void LockedUsed::replace_all_uses(ValueR new_value) {
  TVec<fir::Use> uses_copy;
  {
    auto us = _uses.scoped_lock();
    uses_copy.insert(uses_copy.begin(), us->uses.begin(), us->uses.end());
  }
  for (Use &u : uses_copy) {
    u.replace_use(new_value);
  }
  {
    auto us = _uses.scoped_lock();
    us->uses.clear();
  }
}
void LockedUsed::remove_usage(const Use &use, bool verify) {
  auto us = _uses.scoped_lock();
  return us->remove_usage(use, verify);
}
void LockedUsed::remove_all_usages() {
  auto us = _uses.scoped_lock();
  return us->remove_all_usages();
}

bool Use::operator==(const Use &other) const {
  return user == other.user && type == other.type && argId == other.argId &&
         bbArgId == other.bbArgId;
}
} // namespace foptim::fir

fmt::appender
fmt::formatter<foptim::fir::Use>::format(foptim::fir::Use const &v,
                                         format_context &ctx) const {
  auto out = ctx.out();

  out = fmt::format_to(out, "{:p}",
                       static_cast<const void *>(v.user.get_raw_ptr()));
  switch (v.type) {
  case foptim::fir::UseType::NormalArg:
    return fmt::format_to(out, "({})", v.argId);
  case foptim::fir::UseType::BB:
    return fmt::format_to(out, "<{}>", v.argId);
  case foptim::fir::UseType::BBArg:
    return fmt::format_to(out, "<{}>({})", v.argId, v.bbArgId);
  }
}
