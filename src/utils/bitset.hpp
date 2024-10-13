#pragma once
#include "types.hpp"
#include "utils/vec.hpp"
#include <cassert>

namespace foptim::utils {
struct BitSet;

template <class T> struct IterBitSet {
  size_t indx;
  T &bs;

  bool operator==(IterBitSet<T> other) const { return indx == other.indx; }

  void skip_empty() {
    while (indx < bs._data.size() && !bs[indx]) {
      indx++;
    }
  }

  auto operator++() {
    indx++;
    skip_empty();
    return *this;
  }

  size_t operator*() const {
    ASSERT(bs[indx]);
    return indx;
  }
};

struct BitSet {
  // TODO: FVec
  FVec<bool> _data;

  constexpr BitSet(size_t size, bool val) { _data.resize(size, val); }

  constexpr BitSet &operator=(const BitSet &old) = default;
  constexpr BitSet(const BitSet &old) { _data = old._data; }
  constexpr BitSet &operator=(BitSet &&old) {
    _data = std::move(old._data);
    return *this;
  }
  constexpr BitSet(BitSet &&old) { _data = std::move(old._data); }

  constexpr static BitSet empty(size_t size) { return BitSet{size, false}; }

  constexpr auto operator[](const u32 indx) const { return _data.at(indx); }
  constexpr auto operator[](const u32 indx) { return _data.at(indx); }
  [[nodiscard]] constexpr size_t size() const { return _data.size(); }

  constexpr bool operator==(const BitSet &other) const {
    if (_data.size() != other._data.size()) {
      return false;
    }

    for (size_t i = 0; i < _data.size(); i++) {
      if (_data[i] != other._data[i]) {
        return false;
      }
    }
    return true;
  }

  // constexpr BitSet operator^(const BitSet &other) const {
  //   assert(_data.size() == other._data.size());
  //   BitSet res{_data.size(), false};
  //   for (size_t i = 0; i < _data.size(); i++) {
  //     res._data[i] = _data[i] ^ other._data[i];
  //   }
  //   return res;
  // }

  // constexpr BitSet operator*(const BitSet &other) const {
  //   assert(_data.size() == other._data.size());
  //   BitSet res{_data.size(), 0};
  //   for (size_t i = 0; i < _data.size(); i++) {
  //     res._data[i] = _data[i] && other._data[i];
  //   }
  //   return res;
  // }

  // constexpr BitSet operator+(const BitSet &other) const {
  //   assert(_data.size() == other._data.size());
  //   BitSet res{_data.size(), 0};
  //   for (size_t i = 0; i < _data.size(); i++) {
  //     res._data[i] = _data[i] || other._data[i];
  //   }
  //   return res;
  // }

  // constexpr BitSet operator-(const BitSet &other) const {
  //   assert(_data.size() == other._data.size());
  //   BitSet res{_data.size(), 0};
  //   for (size_t i = 0; i < _data.size(); i++) {
  //     res._data[i] = (!other._data[i]) && _data[i];
  //   }
  //   return res;
  // }

  constexpr BitSet &assign(const BitSet &other) {
    assert(_data.size() == other._data.size());
    for (size_t i = 0; i < _data.size(); i++) {
      _data[i] = other._data[i];
    }
    return *this;
  }

  constexpr BitSet &negate() {
    _data.flip();
    // for (auto && i : _data) {
    //   i = !i;
    // }
    return *this;
  }

  constexpr BitSet &add(const BitSet &other) { return operator+=(other); }

  constexpr BitSet &mul(const BitSet &other) { return operator*=(other); }
  constexpr BitSet &mul_not(const BitSet &other) {
    assert(_data.size() == other._data.size());
    for (size_t i = 0; i < _data.size(); i++) {
      _data[i] = _data[i] && !other._data[i];
    }
    return *this;
  }

  constexpr BitSet &xor_(const BitSet &other) {
    assert(_data.size() == other._data.size());
    for (size_t i = 0; i < _data.size(); i++) {
      _data[i] = _data[i] ^ other._data[i];
    }
    return *this;
  }

  constexpr BitSet &reset(bool val) {
    for (auto &&i : _data) {
      i = val;
    }
    return *this;
  }

  constexpr BitSet &operator*=(const BitSet &other) {
    assert(_data.size() == other._data.size());
    for (size_t i = 0; i < _data.size(); i++) {
      _data[i] = _data[i] && other._data[i];
    }
    return *this;
  }

  constexpr BitSet &operator+=(const BitSet &other) {
    assert(_data.size() == other._data.size());
    for (size_t i = 0; i < _data.size(); i++) {
      _data[i] = _data[i] || other._data[i];
    }
    return *this;
  }

  // returns ~this
  constexpr BitSet operator!() const {
    BitSet res{_data.size(), false};
    for (size_t i = 0; i < _data.size(); i++) {
      res._data[i] = !_data[i];
    }
    return res;
  }

  [[nodiscard]] auto begin() const {
    auto res = IterBitSet{0, *this};
    res.skip_empty();
    return res;
  }
  [[nodiscard]] auto end() const {
    return IterBitSet{this->_data.size(), *this};
  }

  auto begin() {
    auto res = IterBitSet{0, *this};
    res.skip_empty();
    return res;
  }
  auto end() { return IterBitSet{this->_data.size(), *this}; }
};

} // namespace foptim::utils
