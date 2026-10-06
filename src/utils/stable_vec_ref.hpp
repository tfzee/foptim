#pragma once
#include <atomic>
#include <ankerl/unordered_dense.h>

#include "stable_vec_slot.hpp"
#include "types.hpp"
#include "utils/todo.hpp"

namespace foptim::utils {

template <class T> class SRef {
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
    ASSERT(data_ref->generation() == generation);
#endif
    data_ref->mark_free();
  }

  // slot is live and (if tracked) still the one this ref was created for
  [[nodiscard]] constexpr bool is_valid() const {
    if (nullptr == data_ref) [[unlikely]] {
      return false;
    }
    return slot_matches();
  }

  // Checks done on every deref, selected at compile time by SLOT_CHECK_LEVEL
  // (0: none, 1: non null + slot in use, 2: additionally the generation)
  constexpr void verify_validness() const {
#if SLOT_CHECK_LEVEL >= 1
    // a single relaxed load of the slot word covers state and generation
    if (data_ref == nullptr || !slot_matches(std::memory_order_relaxed))
        [[unlikely]] {
      verify_failed();
    }
#endif
  }

#if SLOT_CHECK_LEVEL >= 1
  [[noreturn]] [[gnu::cold]] [[gnu::noinline]] void verify_failed() const {
    ASSERT(data_ref != nullptr);
    ASSERT(data_ref->is_used());
#if SLOT_CHECK_LEVEL >= 2
    ASSERT(generation != 0);
    fmt::println("slot generation {} != ref generation {}",
                 data_ref->generation(), generation);
#endif
    TODO("invalid SRef");
  }
#endif

private:
  [[nodiscard]] constexpr bool slot_matches(
      std::memory_order order = std::memory_order_acquire) const {
    // a live slot never has generation 0, so a ref with 0 never matches
    return data_ref->word.load(order) ==
#ifdef SLOT_CHECK_GENERATION
           slot_word(generation, SlotState::Used);
#else
           slot_word(0, SlotState::Used);
#endif
  }

public:
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
  constexpr SRef() noexcept : data_ref(nullptr) {}
  constexpr SRef(Slot<T> *ref, u32) noexcept : data_ref(ref) {}
#endif
  constexpr static SRef<T> invalid() { return SRef{nullptr, 0}; }
};

template <class T>
constexpr bool operator==(const SRef<T> &self, const SRef<T> &other) {
#ifdef SLOT_CHECK_GENERATION
  return self.data_ref == other.data_ref && self.generation == other.generation;
#else
  return self.data_ref == other.data_ref;
#endif
}

} // namespace foptim::utils

template <class T>
struct ankerl::unordered_dense::hash<foptim::utils::SRef<T>> {
  using is_avalanching = void;

  [[nodiscard]] auto operator()(const foptim::utils::SRef<T> &k) const noexcept
      -> uint64_t {
    using foptim::u32;
#ifdef SLOT_CHECK_GENERATION
    return hash<const void *>()(static_cast<const void *>(k.data_ref)) ^
           hash<u32>()(k.generation);
#else
    return hash<const void *>()(static_cast<const void *>(k.data_ref));
#endif
  }
};

template <class T> struct std::hash<foptim::utils::SRef<T>> {
  std::size_t operator()(const foptim::utils::SRef<T> &k) const {
    using foptim::u32;
    using std::hash;

#ifdef SLOT_CHECK_GENERATION
    return hash<const void *>()(static_cast<const void *>(k.data_ref)) ^
           hash<u32>()(k.generation);
#else
    return hash<const void *>()(static_cast<const void *>(k.data_ref));
#endif
  }
};
