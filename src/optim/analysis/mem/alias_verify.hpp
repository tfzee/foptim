#pragma once
#include "ir/function.hpp"

namespace foptim::optim {

// Checks the access metadata of a function against the module table, prints
// what is wrong.
bool verify_access_meta(const fir::Function &func);

bool verify_module_alias_sets(fir::ContextData &ctx);

}  // namespace foptim::optim
