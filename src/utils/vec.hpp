#pragma once
#include "helpers.hpp"
#include "utils/arena.hpp"
#include <vector>

namespace foptim {

template <class Val, class Alloc = utils::FAlloc<Val>>
using FVec = std::vector<Val, Alloc>;

template <class Val, class Alloc = utils::TempAlloc<Val>>
using TVec = std::vector<Val, Alloc>;

}
