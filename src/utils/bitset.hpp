#pragma once
#include "types.hpp"
#include "utils/arena.hpp"
#include <cassert>

namespace foptim::utils {

template <class StorageType = u64> struct BitRef {
  u16 offset;
  StorageType *_data;

  constexpr BitRef &set(bool value) {
    const auto set_val = *_data | (1 << offset);
    const auto unset_val = *_data & ~(1 << offset);
    *_data = value ? set_val : unset_val;
    // *_data = (*_data & ~(1 << offset)) | ((StorageType)value << offset);
    return *this;
  }

  constexpr operator bool() const { return (*_data >> offset) & 1; }
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
    ASSERT(ref);
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
    auto n_elems = (size + StrgTySizeBit) / StrgTySizeBit;
    _data = Alloc{}.allocate(n_elems);
    reset(val);
  }

  constexpr ~BitSet() {
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    Alloc{}.deallocate(_data, n_elems);
  }

  constexpr BitSet &operator=(const BitSet<StorageType, Alloc> &old) {
    auto n_elems = (old._size_bits + StrgTySizeBit) / StrgTySizeBit;
    auto *new_data = Alloc{}.allocate(n_elems);
    memcpy(new_data, old._data, n_elems * StrgTySizeByte);
    _size_bits = old._size_bits;
    _data = new_data;

    return *this;
  }
  constexpr BitSet(const BitSet &old) {
    auto n_elems = (old._size_bits + StrgTySizeBit) / StrgTySizeBit;
    auto *new_data = Alloc{}.allocate(n_elems);
    memcpy(new_data, old._data, n_elems * StrgTySizeByte);
    _size_bits = old._size_bits;
    _data = new_data;
  }
  constexpr BitSet &operator=(BitSet &&old) {
    _data = std::move(old._data);
    _size_bits = old._size_bits;
    return *this;
  }
  constexpr BitSet(BitSet &&old) {
    _data = std::move(old._data);
    _size_bits = old._size_bits;
  }

  constexpr static BitSet empty(size_t size) { return BitSet{size, false}; }

  constexpr BitRef<StorageType> operator[](const size_t indx) const {
    assert(indx < _size_bits);
    return {(u16)(indx % StrgTySizeBit), &_data[indx / StrgTySizeBit]};
  }
  constexpr BitRef<StorageType> operator[](const size_t indx) {
    assert(indx < _size_bits);
    return {(u16)(indx % StrgTySizeBit), &_data[indx / StrgTySizeBit]};
  }
  [[nodiscard]] constexpr size_t size() const { return _size_bits; }

  constexpr bool operator==(const BitSet &other) const {
    if (size() != other.size()) {
      return false;
    }
    auto n_elems = (_size_bits + StrgTySizeBit) / StrgTySizeBit;
    for (size_t i = 0; i < n_elems; i++) {
      if (_data[i] != other._data[i]) {
        return false;
      }
    }
    return true;
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
