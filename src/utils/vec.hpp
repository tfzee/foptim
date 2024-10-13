#pragma once
#include "helpers.hpp"
#include <vector>

namespace foptim {

template <class Val, class Alloc = utils::FAlloc<Val>>
using FVec = std::vector<Val, Alloc>;

}
