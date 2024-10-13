#pragma once
#include "utils/logging.hpp"
#include "ir/constant_value.hpp"
#include "ir/attribute.hpp"
#include <string>
#include "utils/map.hpp"

namespace foptim::fir {

class Attributable {
  FMap<std::string, Attribute> attribs;

public:
  constexpr const FMap<std::string, Attribute> &
  get_attribs() const {
    return attribs;
  }
  constexpr FMap<std::string, Attribute> &get_attribs() {
    return attribs;
  }

  bool has_attrib(const std::string &key) const;
  const Attribute &get_attrib(const std::string &key) const;
  Attribute &get_attrib(const std::string &key);

  template<class T>
  void clone_attribs(T& other){
    attribs.reserve(other.attribs.size());
    attribs.insert(other.attribs.begin(), other.attribs.end());
  }

  Attributable *add_attrib(std::string key, std::string value);
  Attributable *add_attrib(std::string key, TypeR value);
  Attributable *add_attrib(std::string key, ConstantValue value);
};
} // namespace foptim::fir
