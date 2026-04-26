#pragma once
#include <cstring>
#include <memory>
#include <type_traits>

#include "utils/arena.hpp"
#include "utils/helpers.hpp"

namespace foptim::utils {

namespace {

// used to calculate default storage size
template <class T, size_t wanted_size = 64>
struct SmallVecSizeCalc {
  static constexpr size_t space_for_inline =
      std::max(wanted_size, 1 + sizeof(void*) + sizeof(size_t)) -
      sizeof(void*) - sizeof(size_t);
  static constexpr size_t n_elems_that_fit =
      std::max(1UL, space_for_inline / sizeof(T));

  static_assert(sizeof(T) < 128,
                "Very big object manually set size or use normal vec");
};

// default 16gb for single array should be fine????
// 16gb ought to be enough for anybody :)
template <class T, size_t max_mem = 16ULL * 1024 * 1024 * 1024>
struct SmallVecInferSizeTy {
  static constexpr size_t max_elems = max_mem / sizeof(T);

  // only switch betwee u64 / u32 cause smaller sizes might optimize worse
  // especially when iterating and shit
  // clang-format off
  using Type =
      std::conditional_t<(max_elems <= std::numeric_limits<uint32_t>::max()), uint32_t,
          uint64_t>;
  // clang-format on
};

}  // namespace

template <class T, size_t N = SmallVecSizeCalc<T, 64>::n_elems_that_fit,
          class SizeTy = typename SmallVecInferSizeTy<T>::Type,
          class Allocator = std::allocator<T>>
class SmallVec {
  T* data;  // always valid: points to _inline.buf or heap
  SizeTy _size;

  union {
    struct {
      SizeTy capacity;
    } _heap;
    struct {
      alignas(alignof(T)) char buf[sizeof(T) * N];
    } _inline;
  };

  // return the pointer to the internal local buffer converted correctly to our
  // T
  constexpr T* inline_ptr() noexcept {
    return std::launder(reinterpret_cast<T*>(_inline.buf));
  }
  constexpr const T* inline_ptr() const noexcept {
    return std::launder(reinterpret_cast<const T*>(_inline.buf));
  }

  constexpr void destroy_and_free_all_elems() {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      std::destroy_n(data, _size);
    }
    if (is_heap()) {
      Allocator{}.deallocate(data, _heap.capacity);
    }
    _size = 0;
  }

 public:
  constexpr SmallVec() noexcept : data(inline_ptr()), _size(0) {}
  constexpr T& operator[](size_t i) noexcept { return data[i]; }
  constexpr const T& operator[](size_t i) const noexcept { return data[i]; }
  constexpr SizeTy size() const noexcept { return _size; }
  constexpr SizeTy capacity() const noexcept {
    return is_heap() ? _heap.capacity : N;
  }
  constexpr bool is_heap() const noexcept { return data != inline_ptr(); }
  static constexpr SizeTy max_size() noexcept {
    return std::numeric_limits<SizeTy>::max();
  }
  static_assert(N <= max_size(),
                "Cant allocate more on stack then SizeTy can rerpresent");
  static_assert(sizeof(size_t) >= sizeof(SizeTy),
                "Having SizeTy greater then size_t can lead to issues");

  constexpr void clear() { destroy_and_free_all_elems(); }
  // reserver atleast wanted_new_size.
  //  However it might allocate more if the size of the wanted_new_size is very
  //  close to the old size. This is to prevent some edgecases where people call
  //  reserve often with like size+1 or some like that
  constexpr void reserve_at_least(SizeTy wanted_new_size) {
    if (capacity() >= wanted_new_size) {
      return;
    }
    // cast up to u64 to ensure we can check if it fits for assertion check
    u64 geometric_growth = static_cast<u64>(_size) + (_size / 2);
    u64 actual_new_size =
        std::max(static_cast<u64>(wanted_new_size), geometric_growth);
    ASSERT(actual_new_size <= max_size() &&
           "SmallVec: Capacity exceeds SizeTy limit!");

    bool was_on_heap = is_heap();
    // we know at this point we *MUST* be on the heap otherwise we would fit
    //  to prevent issue when reserving on every push kinda deal
    auto alloc = Allocator{};
    T* new_data = alloc.allocate(static_cast<SizeTy>(actual_new_size));
    // if size is not zero there must be some data either on the stack or the
    // heap
    if constexpr (std::is_trivially_copyable_v<T>) {
      std::memcpy(new_data, data, _size * sizeof(T));
    } else {
      std::uninitialized_move_n(data, _size, new_data);
      if constexpr (!std::is_trivially_destructible_v<T>) {
        std::destroy_n(data, _size);
      }
    }
    if (was_on_heap) {
      alloc.deallocate(data, _heap.capacity);
    }
    data = new_data;
    _heap.capacity = actual_new_size;
  }

  constexpr void push_back(T&& v) {
    if (_size >= capacity()) {
      ASSERT(_size < max_size());
      reserve_at_least(_size + 1);
    }
    std::construct_at(data + _size, std::forward<T>(v));
    _size++;
  }
  constexpr void push_back(const T& v) {
    if (_size >= capacity()) {
      ASSERT(_size < max_size());
      reserve_at_least(_size + 1);
    }
    std::construct_at(data + _size, v);
    _size++;
  }

  template <typename... Args>
  constexpr T& emplace_back(Args&&... args) {
    if (_size >= capacity()) {
      ASSERT(_size < max_size());
      reserve_at_least(_size + 1);
    }
    std::construct_at(data + _size, std::forward<Args>(args)...);
    return data[_size++];
  }

  constexpr void pop_back() {
    if (_size > 0) {
      std::destroy_at(data + _size - 1);
      _size--;
    }
  }

  constexpr const T* begin() const noexcept { return data; }
  constexpr T* end() const noexcept { return data + _size; }
  constexpr T* begin() noexcept { return data; }
  constexpr T* end() noexcept { return data + _size; }

  ~SmallVec() {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      std::destroy_n(data, _size);
    }
    if (is_heap()) {
      Allocator{}.deallocate(data, _heap.capacity);
    }
  }

  constexpr SmallVec(SmallVec&& other) noexcept {
    if (other.is_heap()) {
      _size = other._size;
      data = other.data;
      _heap.capacity = other._heap.capacity;
      other.data = other.inline_ptr();
    } else {
      _size = other._size;
      data = inline_ptr();
      if constexpr (std::is_trivially_copyable_v<T>) {
        std::memcpy(data, other.data, _size * sizeof(T));
      } else {
        std::uninitialized_move_n(other.data, _size, data);
        std::destroy_n(other.data, _size);
      }
    }
    other._size = 0;
  }

  constexpr SmallVec(const SmallVec& other) noexcept {
    if (other.is_heap()) {
      _heap.capacity = other._heap.capacity;
      _size = other._size;
      data = Allocator{}.allocate(_heap.capacity);
    } else {
      _size = other._size;
      data = inline_ptr();
    }
    if constexpr (std::is_trivially_copyable_v<T>) {
      std::memcpy(data, other.data, _size * sizeof(T));
    } else {
      std::uninitialized_copy_n(other.data, _size, data);
    }
  }

  constexpr SmallVec& operator=(SmallVec&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    destroy_and_free_all_elems();
    if (other.is_heap()) {
      _size = other._size;
      data = other.data;
      _heap.capacity = other._heap.capacity;
      other.data = other.inline_ptr();
    } else {
      _size = other._size;
      data = inline_ptr();
      if constexpr (std::is_trivially_copyable_v<T>) {
        std::memcpy(data, other.data, _size * sizeof(T));
      } else {
        std::uninitialized_move_n(other.data, _size, data);
        std::destroy_n(other.data, _size);
      }
    }
    other._size = 0;
    return *this;
  }

  constexpr SmallVec& operator=(const SmallVec& other) noexcept {
    if (this == &other) {
      return *this;
    }
    destroy_and_free_all_elems();
    if (other.is_heap()) {
      _size = other._size;
      _heap.capacity = other._heap.capacity;
      data = Allocator{}.allocate(_heap.capacity);
    } else {
      _size = other._size;
      data = inline_ptr();
    }
    if constexpr (std::is_trivially_copyable_v<T>) {
      std::memcpy(data, other.data, _size * sizeof(T));
    } else {
      std::uninitialized_copy_n(other.data, _size, data);
    }
    return *this;
  }
};

}  // namespace foptim::utils
namespace foptim {
template <class Val,
          size_t N = utils::SmallVecSizeCalc<Val, 64>::n_elems_that_fit,
          class SizeTy = typename utils::SmallVecInferSizeTy<Val>::Type,
          class Alloc = utils::FAlloc<Val>>
using SmallFVec = utils::SmallVec<Val, N, SizeTy, Alloc>;

template <class Val,
          size_t N = utils::SmallVecSizeCalc<Val, 64>::n_elems_that_fit,
          class SizeTy = typename utils::SmallVecInferSizeTy<Val>::Type,
          class Alloc = utils::TempAlloc<Val>>
using SmallTVec = utils::SmallVec<Val, N, SizeTy, Alloc>;

template <class Val,
          size_t N = utils::SmallVecSizeCalc<Val, 64>::n_elems_that_fit,
          class SizeTy = typename utils::SmallVecInferSizeTy<Val>::Type,
          class Alloc = utils::IRAlloc<Val>>
using SmallIRVec = utils::SmallVec<Val, N, SizeTy, Alloc>;
}  // namespace foptim
