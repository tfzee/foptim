#pragma once
#include "types.hpp"
#include "utils/arena.hpp"
#include "utils/logging.hpp"
#include "utils/todo.hpp"
#include <cassert>
#include <cstring>

namespace foptim::utils {

template <class StorageType = u64> struct BitRef {
  u16 offset;
  StorageType *_data;

  constexpr BitRef &set(bool value) {
    const auto set_val = *_data | ((StorageType)1 << offset);
    const auto unset_val = *_data & ~((StorageType)1 << offset);
    *_data = value ? set_val : unset_val;

    ASSERT(offset <= sizeof(StorageType) * 8);
    // *_data = (*_data & ~(1 << offset)) | ((StorageType)value << offset);
    return *this;
  }

  constexpr operator bool() const {
    ASSERT(offset <= sizeof(StorageType) * 8);
    return (*_data >> offset) & 1;
  }
};

template <class StorageType = u64> struct IterBitSet {
  static constexpr u16 StrgTySizeBit = sizeof(StorageType) * 8;
  static constexpr u16 StrgTySizeByte = sizeof(StorageType);
  size_t indx;
  size_t size;
  StorageType *_data;

  bool operator==(IterBitSet<StorageType> other) const {
    return indx == other.indx;
  }

  void skip_empty() {
    while (indx < size) {
      BitRef<StorageType> ref = {(u16)(indx % StrgTySizeBit),
                                 &_data[indx / StrgTySizeBit]};
      if (ref) {
        return;
      }
      indx++;
    }
  }

  auto operator++() {
    indx++;
    skip_empty();
    return *this;
  }

  size_t operator*() const {
    BitRef<StorageType> ref = {(u16)(indx % StrgTySizeBit),
                               &_data[indx / StrgTySizeBit]};
    ASSERT((bool)ref);
    return indx;
  }
};

template <class StorageType = u64, class Alloc = TempAlloc<StorageType>>
struct BitSet {

  static constexpr u16 StrgTySizeBit = sizeof(StorageType) * 8;
  static constexpr u16 StrgTySizeByte = sizeof(StorageType);
  StorageType *_data = nullptr;
  size_t _size_bits;

  constexpr BitSet(size_t size, bool val) : _size_bits(size) {
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    _data = Alloc{}.allocate(n_elems);
    reset(val);
  }

  constexpr ~BitSet() {
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    if (_data) {
      Alloc{}.deallocate(_data, n_elems);
    }
  }
  constexpr auto &operator=(const BitSet<StorageType, Alloc> &old) {
    if (this == &old) {
      return *this;
    }
    const auto n_elems = (old._size_bits + StrgTySizeBit) / StrgTySizeBit;

    Alloc{}.deallocate(_data, n_elems);
    auto *new_data = Alloc{}.allocate(n_elems);
    memcpy(new_data, old._data, n_elems * StrgTySizeByte);
    _data = new_data;
    _size_bits = old._size_bits;

    return *this;
  }

  constexpr BitSet(const BitSet<StorageType, Alloc> &old) {
    const auto n_elems = (old._size_bits + StrgTySizeBit) / StrgTySizeBit;
    Alloc{}.deallocate(_data, n_elems);
    auto *new_data = Alloc{}.allocate(n_elems);
    memcpy(new_data, old._data, n_elems * StrgTySizeByte);
    _data = new_data;
    _size_bits = old._size_bits;
  }

  constexpr auto &operator=(BitSet<StorageType, Alloc> &&old) {
    const auto n_elems = (old._size_bits + StrgTySizeBit) / StrgTySizeBit;
    Alloc{}.deallocate(_data, n_elems);
    _data = old._data;
    _size_bits = old._size_bits;
    old._data = nullptr;
    old._size_bits = 0;
    return *this;
  }

  constexpr BitSet(BitSet<StorageType, Alloc> &&old) {
    const auto n_elems = (old._size_bits + StrgTySizeBit) / StrgTySizeBit;
    Alloc{}.deallocate(_data, n_elems);
    _data = old._data;
    _size_bits = old._size_bits;
    old._data = nullptr;
    old._size_bits = 0;
  }

  constexpr static BitSet empty(size_t size) { return BitSet{size, false}; }

  [[nodiscard]] constexpr bool any() const {
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    for (size_t i = 0; i < n_elems - 1; i++) {
      if (_data[i] != 0) {
        return true;
      }
    }
    u16 n_dead_bits = (n_elems * StrgTySizeBit) - _size_bits;
    u16 n_alive_bits = StrgTySizeBit - n_dead_bits;
    auto mask = ((StorageType)1 << n_alive_bits) - 1;
    return (_data[n_elems - 1] & mask) != 0;
  }

  [[nodiscard]] constexpr bool all() const {
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    for (size_t i = 0; i < n_elems - 1; i++) {
      if (~_data[i] == 0) {
        return true;
      }
    }
    u16 n_dead_bits = (n_elems * StrgTySizeBit) - _size_bits;
    u16 n_alive_bits = StrgTySizeBit - n_dead_bits;
    auto mask = ((StorageType)1 << n_alive_bits) - 1;
    TODO("impl");
    // return (_data[n_elems - 1] & mask) != 0;
  }

  constexpr BitRef<StorageType> operator[](const size_t indx) const {
    ASSERT(indx < _size_bits);
    return {(u16)(indx % StrgTySizeBit), &_data[indx / StrgTySizeBit]};
  }
  constexpr BitRef<StorageType> operator[](const size_t indx) {
    ASSERT(indx < _size_bits);
    return {(u16)(indx % StrgTySizeBit), &_data[indx / StrgTySizeBit]};
  }
  [[nodiscard]] constexpr size_t size() const { return _size_bits; }

  constexpr bool operator==(const BitSet &other) const {
    if (size() != other.size()) {
      return false;
    }
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    for (size_t i = 0; i < n_elems - 1; i++) {
      if (_data[i] != other._data[i]) {
        return false;
      }
    }

    u16 n_dead_bits = (n_elems * StrgTySizeBit) - _size_bits;
    u16 n_alive_bits = StrgTySizeBit - n_dead_bits;
    auto mask = ((StorageType)1 << n_alive_bits) - 1;
    return (_data[n_elems - 1] & mask) == (other._data[n_elems - 1] & mask);
  }

  constexpr BitSet &set(size_t indx, u8 width, u64 value) {
    ASSERT(width <= 64);
    for (u32 i = 0; i < width; i++) {
      auto loc = operator[](indx + i);
      // BitRef<> loc = {(u16)((indx + i) % StrgTySizeBit),
      //                 &_data[(indx + i) / StrgTySizeBit]};
      bool bitset = ((value >> i) & 0b1) == 1;
      loc.set(bitset);
    }
    return *this;
  }

  [[nodiscard]] constexpr u64 get(size_t indx, u8 width) const {
    ASSERT(width <= 64);
    u64 out_val = 0;
    for (u32 ip1 = width; ip1 > 0; ip1--) {
      u32 i = ip1 - 1;
      auto loc = operator[](indx + i);
      // BitRef<> loc = {(u16)((indx + i) % StrgTySizeBit),
      //                 &_data[(indx + i) / StrgTySizeBit]};
      out_val = (out_val << 1) | (u64)(bool)loc;
    }
    return out_val;
  }

  constexpr BitSet &assign(const BitSet &other) {
    assert(_size_bits == other._size_bits);
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    for (size_t i = 0; i < n_elems; i++) {
      _data[i] = other._data[i];
    }
    return *this;
  }

  constexpr BitSet &negate() {
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    for (size_t i = 0; i < n_elems; i++) {
      _data[i] = ~_data[i];
    }
    // for (auto &&i : _data) {
    //   i = ~i;
    // }
    return *this;
  }

  constexpr BitSet &add(const BitSet &other) { return operator+=(other); }

  constexpr BitSet &mul(const BitSet &other) { return operator*=(other); }
  constexpr BitSet &mul_not(const BitSet &other) {
    assert(_size_bits == other._size_bits);
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    for (size_t i = 0; i < n_elems; i++) {
      _data[i] = _data[i] & ~other._data[i];
    }
    return *this;
  }

  constexpr BitSet &xor_(const BitSet &other) {
    assert(_size_bits == other._size_bits);
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    for (size_t i = 0; i < n_elems; i++) {
      _data[i] = _data[i] ^ other._data[i];
    }
    return *this;
  }

  constexpr BitSet &reset(bool val) {
    u8 fill_val = val ? ~0 : 0;
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    memset(_data, fill_val, n_elems * StrgTySizeByte);
    // for (auto &&i : _data) {
    //   i = val;
    // }
    return *this;
  }

  constexpr BitSet &operator*=(const BitSet &other) {
    assert(_size_bits == other._size_bits);
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    for (size_t i = 0; i < n_elems; i++) {
      _data[i] = _data[i] & other._data[i];
    }
    return *this;
  }

  constexpr BitSet &operator+=(const BitSet &other) {
    assert(_size_bits == other._size_bits);
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    for (size_t i = 0; i < n_elems; i++) {
      _data[i] = _data[i] | other._data[i];
    }
    return *this;
  }

  // returns ~this
  // constexpr BitSet operator!() const {
  //   BitSet res{_data.size(), false};
  //   for (size_t i = 0; i < _data.size(); i++) {
  //     res._data[i] = !_data[i];
  //   }
  //   return res;
  // }

  [[nodiscard]] auto begin() const {
    auto res = IterBitSet<StorageType>{0, _size_bits, _data};
    res.skip_empty();
    return res;
  }
  [[nodiscard]] auto end() const {
    return IterBitSet<StorageType>{_size_bits, _size_bits, _data};
  }

  [[nodiscard]] auto begin() {
    auto res = IterBitSet<StorageType>{0, _size_bits, _data};
    res.skip_empty();
    return res;
  }
  [[nodiscard]] auto end() {
    return IterBitSet<StorageType>{_size_bits, _size_bits, _data};
  }
};

} // namespace foptim::utils

template <class T, class Alloc>
class fmt::formatter<foptim::utils::BitSet<T, Alloc>>
    : public BaseIRFormatter<foptim::utils::BitSet<T, Alloc>> {
public:
  appender format(foptim::utils::BitSet<T, Alloc> const &sett,
                  format_context &ctx) const {

    auto app = ctx.out();
    for (size_t i = 0; i < sett._size_bits; i++) {
      app = fmt::format_to(app, "{}", (sett[i] ? '1' : '0'));
    }
    return app;
  }
};
