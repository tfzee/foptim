#include "parameters.hpp"

namespace foptim::utils {

std::string out_file_path;
std::string in_file_path;
bool print_optimization_failure_reasons = true;
u8 number_worker_threads = 0;

bool enable_avx512f = true;
bool enable_avx512bw = true;
bool enable_avx512cd = true;
bool enable_avx512dq = true;
bool enable_avx512vl = true;
bool all_linkage_internal = false;
bool assume_cstdlib_beheaviour = true;

}  // namespace foptim::utils
