#include "dce.hpp"
#include "mir/analysis/live_variables.hpp"

namespace foptim::fmir {

void DeadCodeElim::apply(MFunc &func) {
  CFG cfg{func};
  LiveVariables live{cfg, func};
}

void DeadCodeElim::apply(FVec<MFunc> &funcs) {
  ZoneScopedN("DCE");
  for (auto &func : funcs) {
    apply(func);
  }
}

} // namespace foptim::fmir
