#include "ir/context.hpp"
#include "utils/job_system.hpp"

namespace foptim::optim::pipeline {

// run the pipeline specified in the config file under the optim section
void optimize_fir(foptim::fir::Context &ctx, foptim::JobSheduler *shed);

}  // namespace foptim::conf::pipeline
