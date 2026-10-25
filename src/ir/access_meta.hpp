#pragma once
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "utils/map.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::fir {

/*
  What is known about the memory a load or store touches, as sets of the
  module alias set table. The access lies in the intersection of the type and
  scope sets and does not alias anything inside the not_in sets. No metadata
  means unknown.
*/
struct AccessMeta {
  IRVec<u32> types;
  // one set or union per scope domain
  IRVec<u32> scope_in;
  IRVec<u32> not_in;

  [[nodiscard]] bool empty() const {
    return types.empty() && scope_in.empty() && not_in.empty();
  }
  bool operator==(const AccessMeta &o) const = default;
};

// What a copy of an access keeps. A scope is one dynamic instance of the code,
// so a copy that runs next to the original in a loop may only keep the types.
enum class MetaCopy : u8 { None, Types, All };

class AccessMetaTable {
 public:
  [[nodiscard]] const AccessMeta *get(Instr i) const {
    if (!i->has_access_meta) {
      return nullptr;
    }
    auto it = map.find(i);
    return it == map.end() ? nullptr : &it->second;
  }
  void set(Instr i, AccessMeta m) {
    if (m.empty()) {
      i->has_access_meta = false;
      map.erase(i);
      return;
    }
    i->has_access_meta = true;
    map[i] = std::move(m);
  }
  void erase(Instr i) {
    i->has_access_meta = false;
    map.erase(i);
  }

 private:
  FDMap<Instr, AccessMeta> map;
};

inline AccessMeta filter_meta(const AccessMeta &m, MetaCopy how) {
  AccessMeta r;
  if (how == MetaCopy::None) {
    return r;
  }
  r.types = m.types;
  if (how == MetaCopy::All) {
    r.scope_in = m.scope_in;
    r.not_in = m.not_in;
  }
  return r;
}

}  // namespace foptim::fir
