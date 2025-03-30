#include "use.hpp"
#include "ir/instruction_data.hpp"
#include "value.hpp"

namespace foptim::fir {
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

void Used::replace_all_uses(ValueR new_value) {
  auto uses_copy = uses;
  for (Use &u : uses_copy) {
    u.replace_use(new_value);
  }
  uses.clear();
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
    fmt::println("USE: {}", u);
    fmt::println("USER: {}", u.user);
    ASSERT_M(false, "Failed to find usage that was to be removed");
  }
}

void Used::remove_all_usages() { replace_all_uses(ValueR()); }

bool Use::operator==(const Use &other) const {
  return user == other.user && type == other.type && argId == other.argId &&
         bbArgId == other.bbArgId;
}
} // namespace foptim::fir

fmt::appender
fmt::formatter<foptim::fir::Use>::format(foptim::fir::Use const &v,
                                         format_context &ctx) const {
  auto out = ctx.out();

  out = fmt::format_to(out, "{:p}", (void *)v.user.get_raw_ptr());
  switch (v.type) {
  case foptim::fir::UseType::NormalArg:
    return fmt::format_to(out, "({})", v.argId);
  case foptim::fir::UseType::BB:
    return fmt::format_to(out, "<{}>", v.argId);
  case foptim::fir::UseType::BBArg:
    return fmt::format_to(out, "<{}>({})", v.argId, v.bbArgId);
  }
}
