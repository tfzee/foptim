#pragma once
#include <ankerl/unordered_dense.h>

#include "stable_vec_slot.hpp"
#include "types.hpp"
#include "utils/todo.hpp"
// needed for std::hash
#include <unordered_map>

namespace foptim::utils {

template <class T>
class SRef {
 public:
  Slot<T> *data_ref;
#ifdef SLOT_CHECK_GENERATION
  u32 generation;
#endif
  constexpr bool operator==(const SRef<T> &other) const {
#ifdef SLOT_CHECK_GENERATION
    return generation == other.generation && data_ref == other.data_ref;
#else
    return data_ref == other.data_ref;
#endif
  }

  constexpr bool operator<(const SRef<T> &other) const {
#ifdef SLOT_CHECK_GENERATION
    if (generation == other.generation) {
      return data_ref < other.data_ref;
    }
    return generation < other.generation;
#else
    return data_ref < other.data_ref;
#endif
  }

  constexpr void _invalidate() {
    ASSERT(data_ref != nullptr);
#ifdef SLOT_CHECK_GENERATION
    ASSERT(generation != 0);
    ASSERT(data_ref->generation == generation);
#endif
    data_ref->used = SlotState::Free;
  }

  [[nodiscard]] constexpr bool is_valid() const {
    if (nullptr == data_ref) {
      return false;
    }
#ifdef SLOT_CHECK_GENERATION
    if (0 == generation) {
      return false;
    }
    if (data_ref->generation != generation) {
      return false;
    }
#endif
    if (data_ref->used != SlotState::Used) {
      return false;
    }
    return true;
  }

  inline constexpr void verify_validness() const {
    ASSERT(data_ref != nullptr && data_ref->used == SlotState::Used);
#ifdef SLOT_CHECK_GENERATION
    ASSERT(generation != 0);
    if (data_ref->generation != generation) {
      fmt::println("{} {}", data_ref->generation.load(), generation);
      TODO("shite");
    }
    ASSERT(data_ref->generation == generation);
#endif
  }

  constexpr const T *get_raw_ptr() const {
    verify_validness();
    return &data_ref->data;
  }

  constexpr T *operator->() {
    verify_validness();
    return &this->data_ref->data;
  }
  constexpr const T *operator->() const {
    verify_validness();
    return &this->data_ref->data;
  }
  // constexpr SRef(const SRef<T> &&old)
  //     : data_ref(old.data_ref), generation(old.generation) {}
  // constexpr SRef(SRef<T> &old)
  //     : data_ref(old.data_ref), generation(old.generation) {}
#ifdef SLOT_CHECK_GENERATION
  constexpr SRef(std::nullptr_t) noexcept : data_ref(nullptr), generation(0) {}
  constexpr SRef() noexcept : data_ref(nullptr), generation(0) {}
  constexpr SRef(Slot<T> *ref, u32 gen) noexcept
      : data_ref(ref), generation(gen) {}
#else
  constexpr SRef(std::nullptr_t) noexcept : data_ref(nullptr) {}
  constexpr SRef() : data_ref(nullptr) noexcept {}
  constexpr SRef(Slot<T> *ref, u32) noexcept : data_ref(ref) {}
#endif
  constexpr static SRef<T> invalid() { return SRef{nullptr, 0}; }
};

template <class T>
constexpr inline bool operator==(const SRef<T> &self, const SRef<T> &other) {
#ifdef SLOT_CHECK_GENERATION
  return self.data_ref == other.data_ref && self.generation == other.generation;
#else
  return self.data_ref == other.data_ref;
#endif
}

}  // namespace foptim::utils

template <class T>
struct ankerl::unordered_dense::hash<foptim::utils::SRef<T>> {
  using is_avalanching = void;

  [[nodiscard]] auto operator()(const foptim::utils::SRef<T> &k) const noexcept
      -> uint64_t {
    using foptim::u32;
#ifdef SLOT_CHECK_GENERATION
    return hash<void *>()((void *)k.data_ref) ^ hash<u32>()(k.generation);
#else
    return hash<void *>()((void *)k.data_ref);
#endif
  }
};

template <class T>
struct std::hash<foptim::utils::SRef<T>> {
  std::size_t operator()(const foptim::utils::SRef<T> &k) const {
    using foptim::u32;
    using std::hash;

#ifdef SLOT_CHECK_GENERATION
    return hash<void *>()((void *)k.data_ref) ^ hash<u32>()(k.generation);
#else
    return hash<void *>()((void *)k.data_ref);
#endif
  }
};
