#pragma once
#include "../func.hpp"
#include "mir/global.hpp"
#include "utils/job_system.hpp"

namespace foptim::fmir::pipeline {

// run the pipeline specified in the config file under the optim section
void optimize_mir(foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::TVec<foptim::fir::Function *> &reordered_funcs,
                  foptim::FVec<foptim::fmir::Global> & globals,
                  foptim::JobSheduler *shed, const conf::CompConf &config);

} // namespace foptim::fmir::pipeline
