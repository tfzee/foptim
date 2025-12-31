#pragma once
#include <string>

#include "utils/types.hpp"

namespace foptim::utils {

extern std::string out_file_path;
extern std::string in_file_path;
extern bool print_optimization_failure_reasons;
extern u8 number_worker_threads;

extern bool enable_avx512f;
extern bool enable_avx512bw;
extern bool enable_avx512cd;
extern bool enable_avx512dq;
extern bool enable_avx512vl;
// makes everything interal if possible
extern bool all_linkage_internal;

// assumes that calls to functions named like strlen and other cstdlib function
// beheave like in the cstdlib
extern bool assume_cstdlib_beheaviour;

}  // namespace foptim::utils
