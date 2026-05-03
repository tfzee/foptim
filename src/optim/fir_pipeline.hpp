#include "ir/context.hpp"
#include "utils/job_system.hpp"

namespace foptim::conf::pipeline {

void optimize_fir(foptim::fir::Context &ctx, foptim::JobSheduler *shed);

}  // namespace foptim::conf::pipeline
