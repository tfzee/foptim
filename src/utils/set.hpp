
#pragma once
#include "helpers.hpp"
#include "utils/arena.hpp"
#include <unordered_set>

namespace foptim {

template <class Val, class Alloc = utils::FAlloc<Val>>
using FSet = std::unordered_set<Val, std::hash<Val>, std::equal_to<Val>, Alloc>;

template <class Val, class Alloc = utils::TempAlloc<Val>>
using TSet = std::unordered_set<Val, std::hash<Val>, std::equal_to<Val>, Alloc>;
} // namespace foptim
