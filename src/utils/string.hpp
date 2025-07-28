#pragma once
#include <string>

#include "utils/arena.hpp"

namespace foptim {
using TString =
    std::basic_string<char, std::char_traits<char>, utils::TempAlloc<char>>;

// TODO: these just copy everytime you copy it but since lifetimes are bound to
// the arena they should be not copying unless modified?

using IRString =
    std::basic_string<char, std::char_traits<char>, utils::IRAlloc<char>>;

using IRStringRef = const char*;

}  // namespace foptim
