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

template <class K, class V, class Hash, class Equal, class Alloc>
utils::Printer
operator<<(utils::Printer p,
           const std::unordered_map<K, V, Hash, Equal, Alloc> sett) {
  p << "{";
  for (const auto &[key, value] : sett) {
    p << key << ": " << value << ",\n";
  }
  return p << "}";
}
} // namespace foptim
