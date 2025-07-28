#pragma once
#include <map>
#include <unordered_map>

#include "helpers.hpp"
#include "utils/arena.hpp"
#include "utils/logging.hpp"

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
          class Alloc = utils::TempAlloc<std::pair<const Key, Val>>>
using TOMap = std::map<Key, Val, std::less<Key>, Alloc>;

template <class Key, class Val,
          class Alloc = utils::IRAlloc<std::pair<const Key, Val>>>
using IRMap =
    std::unordered_map<Key, Val, std::hash<Key>, std::equal_to<Key>, Alloc>;

}  // namespace foptim
