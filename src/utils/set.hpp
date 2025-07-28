#pragma once
#include <unordered_set>

#include "helpers.hpp"
#include "utils/arena.hpp"

namespace foptim {

template <class Val, class Alloc = utils::FAlloc<Val>>
using FSet = std::unordered_set<Val, std::hash<Val>, std::equal_to<Val>, Alloc>;

template <class Val, class Alloc = utils::TempAlloc<Val>>
using TSet = std::unordered_set<Val, std::hash<Val>, std::equal_to<Val>, Alloc>;

template <class Val, class Alloc = utils::IRAlloc<Val>>
using IRSet =
    std::unordered_set<Val, std::hash<Val>, std::equal_to<Val>, Alloc>;
}  // namespace foptim
