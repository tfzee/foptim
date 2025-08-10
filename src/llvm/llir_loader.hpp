#pragma once
#include "ir/context.hpp"

namespace foptim {
class JobSheduler;
};

void load_llvm_ir(const char *filename, foptim::fir::Context &fctx,
                  foptim::JobSheduler &shed);
