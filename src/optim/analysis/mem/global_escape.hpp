#pragma once
#include "ir/context.hpp"
#include "optim/analysis/analysis_manager.hpp"

namespace foptim::optim {

// Sets GlobalData::addr_private of every global. ONLY CALL FROM MODULE PASS.
// Reports every global that changed, and drops the memory analyses of its
// users, they were built on the old answer.
void update_private_globals(fir::Context &ctx, AnalysisManager &analyMan);

// false if a global flagged private has an escaping use now, which means a pass
// created one after the flags were set. Meant for assert builds.
bool verify_private_globals(fir::Context &ctx);

}  // namespace foptim::optim
