#pragma once
#include "utils/types.hpp"

namespace foptim::utils {

// At 0 only what the user asked for is printed (--print-mir, --print-fir,
// time_passes, errors), everything else (progress, warnings, stats, timings)
// needs a verbosity above 0.
inline u8 verbosity = 254;

[[nodiscard]] inline bool verbose() { return verbosity > 0; }

} // namespace foptim::utils
