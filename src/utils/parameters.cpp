#include "parameters.hpp"

namespace foptim::utils {

std::string out_file_path;
std::string in_file_path;
bool print_optimization_failure_reasons = true;
u8 number_worker_threads = 0;

bool enable_avx512f = false;
bool enable_avx512bw = false;
bool enable_avx512cd = false;
bool enable_avx512dq = false;
bool enable_avx512vl = false;
bool all_linkage_internal = false;
bool assume_cstdlib_beheaviour = true;

}  // namespace foptim::utils
