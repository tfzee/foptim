#include "attributable.hpp"
#include "attribute.hpp"

namespace foptim::fir {

bool Attributable::has_attrib(const std::string &key) const {
  return attribs.contains(key);
}
const Attribute &Attributable::get_attrib(const std::string &key) const {
  return attribs.at(key);
}
Attribute &Attributable::get_attrib(const std::string &key) {
  return attribs.at(key);
}
Attributable *Attributable::add_attrib(std::string key, std::string value) {
  if (attribs.contains(key)) {
    utils::Debug << "add_attrib Warning: Key '" << key
                 << "' was already a attrib with value '" << attribs[key]
                 << "'\n";
  }
  attribs.insert({key, Attribute(value)});
  return this;
}

Attributable  *Attributable::add_attrib(std::string key, TypeR value) {
  if (attribs.contains(key)) {
    utils::Debug << "add_attrib Warning: Key '" << key
                 << "' was already a attrib with value '" << attribs[key]
                 << "'\n";
  }
  attribs.insert({key, Attribute(value)});
  return this;
}

Attributable *Attributable::add_attrib(std::string key, ConstantValue value) {
  attribs.insert({key, Attribute(value)});
  return this;
}
} // namespace foptim::fir
