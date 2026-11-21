#pragma once
#include "../func.hpp"
#include "mir/global.hpp"
#include "utils/job_system.hpp"

namespace foptim::fmir::pipeline {

// select the MIR of every defined function, the FIR is not needed afterwards
void match_functions(foptim::FVec<foptim::fmir::MFunc> &funcs,
                     foptim::TVec<foptim::fir::Function *> &reordered_funcs,
                     foptim::JobSheduler *shed, const conf::CompConf &config);

// run the MIR passes on the matched functions
void run_mir_passes(foptim::FVec<foptim::fmir::MFunc> &funcs,
                    foptim::JobSheduler *shed, const conf::CompConf &config);

// run the pipeline specified in the config file under the optim section
void optimize_mir(foptim::FVec<foptim::fmir::MFunc> &funcs,
                  foptim::TVec<foptim::fir::Function *> &reordered_funcs,
                  foptim::FVec<foptim::fmir::Global> &globals,
                  foptim::JobSheduler *shed, const conf::CompConf &config);

}  // namespace foptim::fmir::pipeline
