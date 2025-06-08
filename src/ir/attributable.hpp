#pragma once
#include "ir/attribute.hpp"
#include "ir/constant_value.hpp"
#include "utils/logging.hpp"
#include "utils/map.hpp"
#include "utils/string.hpp"

namespace foptim::fir {

class Attributable {
  IRMap<IRString, Attribute> attribs;

public:
  [[nodiscard]] constexpr const auto &get_attribs() const { return attribs; }
  [[nodiscard]] constexpr auto &get_attribs() { return attribs; }

  [[nodiscard]] bool has_attrib(const IRString &key) const;
  [[nodiscard]] const Attribute &get_attrib(const IRString &key) const;
  [[nodiscard]] Attribute &get_attrib(const IRString &key);

  template <class T> void clone_attribs(T &other) {
    attribs.reserve(other.attribs.size());
    attribs.insert(other.attribs.begin(), other.attribs.end());
  }

  Attributable *add_attrib(IRString key, IRStringRef value);
  Attributable *add_attrib(IRString key, TypeR value);
  Attributable *add_attrib(IRString key, ConstantValueR value);

  Attributable *set_attrib(IRString key, IRStringRef value);
  Attributable *set_attrib(IRString key, TypeR value);
  Attributable *set_attrib(IRString key, ConstantValueR value);
};
} // namespace foptim::fir
