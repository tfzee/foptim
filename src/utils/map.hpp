#pragma once
#include "helpers.hpp"
#include "utils/arena.hpp"
#include "utils/logging.hpp"
#include <unordered_map>

namespace foptim {

template <class Key, class Val,
          class Alloc = utils::FAlloc<std::pair<const Key, Val>>>
using FMap =
    std::unordered_map<Key, Val, std::hash<Key>, std::equal_to<Key>, Alloc>;

template <class Key, class Val,
          class Alloc = utils::TempAlloc<std::pair<const Key, Val>>>
using TMap =
    std::unordered_map<Key, Val, std::hash<Key>, std::equal_to<Key>, Alloc>;

template <class Key, class Val,
          class Alloc = utils::IRAlloc<std::pair<const Key, Val>>>
using IRMap =
    std::unordered_map<Key, Val, std::hash<Key>, std::equal_to<Key>, Alloc>;

} // namespace foptim
