#pragma once
#include "ir/attribute.hpp"
#include "ir/constant_value.hpp"
#include "utils/logging.hpp"
#include "utils/map.hpp"
#include <string>

namespace foptim::fir {

class Attributable {
  IRMap<std::string, Attribute> attribs;

public:
  [[nodiscard]] constexpr const auto &get_attribs() const { return attribs; }
  [[nodiscard]] constexpr auto &get_attribs() { return attribs; }

  [[nodiscard]] bool has_attrib(const std::string &key) const;
  [[nodiscard]] const Attribute &get_attrib(const std::string &key) const;
  [[nodiscard]] Attribute &get_attrib(const std::string &key);

  template <class T> void clone_attribs(T &other) {
    attribs.reserve(other.attribs.size());
    attribs.insert(other.attribs.begin(), other.attribs.end());
  }

  Attributable *add_attrib(std::string key, std::string value);
  Attributable *add_attrib(std::string key, TypeR value);
  Attributable *add_attrib(std::string key, ConstantValue value);
};
} // namespace foptim::fir
