#pragma once
#include "utils/types.hpp"
#include <string>

namespace foptim::utils {

extern std::string out_file_path;
extern std::string in_file_path;
extern bool print_optimization_failure_reasons;
extern u8 number_worker_threads;
extern bool all_linkage_internal;

}
