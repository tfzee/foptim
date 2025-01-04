#include "use.hpp"
#include "value.hpp"

namespace foptim::fir {
void Used::replace_all_uses(ValueR new_value) {
  auto uses_copy = uses;
  for (Use &u : uses_copy) {
    switch (u.type) {
    case UseType::NormalArg:
      u.user.replace_arg(u.argId, new_value);
      break;
    case UseType::BBArg:
      u.user.replace_bb_arg(u.argId, u.bbArgId, new_value);
      break;
    case UseType::BB:
      u.user.replace_bb(u.argId, new_value.as_bb());
      break;
    }
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
    utils::Debug << "USE: " << u << "\n";
    utils::Debug << "USER: " << u.user << "\n";
    ASSERT_M(false, "Failed to find usage that was to be removed");
  }
}

void Used::remove_all_usages() { replace_all_uses(ValueR()); }
} // namespace foptim::fir
