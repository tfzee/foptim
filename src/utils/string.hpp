#pragma once
#include "helpers.hpp"
#include "utils/arena.hpp"
#include <string>

namespace foptim {
using TString =
    std::basic_string<char, std::char_traits<char>, utils::TempAlloc<char>>;

using IRString =
    std::basic_string<char, std::char_traits<char>, utils::IRAlloc<char>>;

} // namespace foptim
