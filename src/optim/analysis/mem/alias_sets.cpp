#include "optim/analysis/mem/alias_sets.hpp"

#include <fmt/base.h>

#include <algorithm>

#include "utils/map.hpp"
#include "utils/todo.hpp"

namespace foptim::optim {

namespace {
u64 hash_parts(const FVec<SetId> &parts) {
  u64 h = parts.size();
  for (auto p : parts) {
    h = ankerl::unordered_dense::detail::wyhash::mix(h ^ p,
                                                     0x9e3779b97f4a7c15ULL);
  }
  return h;
}
}  // namespace

bool AliasSetTable::has(const FVec<SetId> &v, SetId s) {
  return std::ranges::find(v, s) != v.end();
}

const AliasSetTable &AliasSetTable::roots() {
  static const AliasSetTable table{RootsTag{}};
  return table;
}

AliasSetTable::AliasSetTable(RootsTag) {
  create(std::span<const SetId>{});  // Unknown
  const SetId top[] = {Unknown};
  create(top);  // Foreign
  const SetId foreign[] = {Foreign};
  create(foreign);  // Null
  create(top);      // Stack
  create(foreign);  // Globals
  create(foreign);  // Heap
  const SetId globals[] = {Globals};
  create(globals);  // ReadOnlyGlobals
  create(foreign);  // Incoming
  const SetId incoming[] = {Incoming};
  create(incoming);  // IncomingNonNull
  create(foreign);   // Restrict
  ASSERT(size() == NumRoots);

  const SetId disjoint_roots[] = {Null, Stack, Globals, Heap};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      declare_disjoint(disjoint_roots[i], disjoint_roots[j]);
    }
  }
  declare_disjoint(Incoming, Stack);
  declare_disjoint(Incoming, Heap);
  declare_disjoint(IncomingNonNull, Null);
  for (auto other : {Incoming, Stack, Globals, Heap}) {
    declare_disjoint(Restrict, other);
  }
}

AliasSetTable::AliasSetTable(const AliasSetTable &parent)
    : parent(&parent), base(parent.size()) {}

const AliasSetTable::Node &AliasSetTable::node(SetId s) const {
  if (s < base) {
    return parent->node(s);
  }
  ASSERT(s - base < nodes.size());
  return nodes[s - base];
}

const FVec<SetId> *AliasSetTable::declared(SetId s) const {
  auto it = decl.find(s);
  return it == decl.end() ? nullptr : &it->second;
}

std::span<const SetId> AliasSetTable::parts(SetId s) const {
  return node(s).parts;
}

SetId AliasSetTable::add_node(Node n) {
  nodes.push_back(std::move(n));
  return base + static_cast<SetId>(nodes.size() - 1);
}

SetId AliasSetTable::create(std::span<const SetId> parents) {
  Node n;
  const SetId id = base + static_cast<SetId>(nodes.size());
  n.anc.push_back(id);
  for (auto p : parents) {
    ASSERT(p < id);
    for (auto a : node(p).anc) {
      if (!has(n.anc, a)) {
        n.anc.push_back(a);
      }
    }
  }
  if (id != Unknown && !has(n.anc, Unknown)) {
    n.anc.push_back(Unknown);
  }
  return add_node(std::move(n));
}

SetId AliasSetTable::create_object(SetId root,
                                   std::span<const SetId> extra_parents) {
  FVec<SetId> parents{root};
  parents.insert(parents.end(), extra_parents.begin(), extra_parents.end());
  const SetId id = create(parents);
  nodes[id - base].group = root;
  return id;
}

void AliasSetTable::declare_disjoint(SetId a, SetId b) {
  ASSERT(a != b && a != Unknown && b != Unknown);
  ASSERT(!subset(a, b) && !subset(b, a));
  auto add = [&](SetId x, SetId y) {
    auto &v = decl[x];
    if (!has(v, y)) {
      v.push_back(y);
    }
  };
  add(a, b);
  add(b, a);
}

FVec<SetId> AliasSetTable::leaves_of(SetId s) const {
  const auto &p = node(s).parts;
  return p.empty() ? FVec<SetId>{s} : p;
}

std::optional<SetId> AliasSetTable::find_union(const FVec<SetId> &parts,
                                               u64 hash) const {
  if (parent != nullptr) {
    if (auto r = parent->find_union(parts, hash)) {
      return r;
    }
  }
  auto it = unions.find(hash);
  if (it == unions.end()) {
    return std::nullopt;
  }
  for (auto id : it->second) {
    if (node(id).parts == parts) {
      return id;
    }
  }
  return std::nullopt;
}

SetId AliasSetTable::widen(const FVec<SetId> &parts) const {
  // the deepest set that is above all parts
  SetId best = Unknown;
  size_t best_depth = 0;
  for (auto cand : node(parts[0]).anc) {
    bool common = true;
    for (auto p : parts) {
      common = common && leaf_subset(p, cand);
    }
    if (common && node(cand).anc.size() > best_depth) {
      best = cand;
      best_depth = node(cand).anc.size();
    }
  }
  return best;
}

SetId AliasSetTable::union_of(SetId a, SetId b) {
  if (a == b) {
    return a;
  }
  FVec<SetId> all = leaves_of(a);
  for (auto p : leaves_of(b)) {
    if (!has(all, p)) {
      all.push_back(p);
    }
  }
  // a part inside another part adds nothing
  FVec<SetId> parts;
  for (auto p : all) {
    bool inside = false;
    for (auto q : all) {
      inside = inside || (q != p && leaf_subset(p, q));
    }
    if (!inside) {
      parts.push_back(p);
    }
  }
  std::ranges::sort(parts);
  if (parts.size() == 1) {
    return parts[0];
  }
  if (parts.size() > MaxParts) {
    return widen(parts);
  }
  const u64 h = hash_parts(parts);
  if (auto found = find_union(parts, h)) {
    return *found;
  }
  Node n;
  const SetId id = base + static_cast<SetId>(nodes.size());
  n.anc.push_back(id);
  // the sets that are above every part
  for (auto cand : node(parts[0]).anc) {
    bool common = true;
    for (auto p : parts) {
      common = common && leaf_subset(p, cand);
    }
    if (common) {
      n.anc.push_back(cand);
    }
  }
  n.parts = parts;
  const SetId made = add_node(std::move(n));
  unions[h].push_back(made);
  return made;
}

bool AliasSetTable::subset(SetId a, SetId b) const {
  if (a == b) {
    return true;
  }
  const auto &nb = node(b);
  if (nb.parts.empty()) {
    return has(node(a).anc, b);
  }
  // every part of a has to be inside some part of b
  for (auto pa : leaves_of(a)) {
    bool inside = false;
    for (auto pb : nb.parts) {
      inside = inside || leaf_subset(pa, pb);
    }
    if (!inside) {
      return false;
    }
  }
  return true;
}

bool AliasSetTable::leaf_disjoint(SetId a, SetId b) const {
  if (a == b) {
    return false;
  }
  const auto &na = node(a);
  const auto &nb = node(b);
  for (auto x : na.anc) {
    const SetId gx = node(x).group;
    if (gx != 0) {
      for (auto y : nb.anc) {
        if (y != x && node(y).group == gx) {
          return true;
        }
      }
    }
    // the declarations of x may be in this table or in the parent
    for (const AliasSetTable *t = this; t != nullptr; t = t->parent) {
      if (const auto *d = t->declared(x)) {
        for (auto y : *d) {
          if (has(nb.anc, y)) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool AliasSetTable::verify() const {
  bool ok = true;
  auto fail = [&](SetId s, const char *what) {
    fmt::println(stderr, "AliasSetTable: set {} {}", s, what);
    ok = false;
  };
  for (size_t i = 0; i < nodes.size(); i++) {
    const SetId id = base + static_cast<SetId>(i);
    const Node &n = nodes[i];
    if (n.anc.empty() || n.anc[0] != id) {
      fail(id, "does not start its ancestors with itself");
      continue;
    }
    if (id != Unknown && !has(n.anc, Unknown)) {
      fail(id, "is not below Unknown");
    }
    for (auto a : n.anc) {
      if (a >= size() || a > id) {
        fail(id, "has an ancestor that does not exist or is newer");
      }
    }
    if (n.group != 0 && n.group >= id) {
      fail(id, "has a group that does not exist");
    }
    if (!n.parts.empty()) {
      if (n.parts.size() < 2 || n.parts.size() > MaxParts) {
        fail(id, "is a union with a wrong number of parts");
      }
      for (size_t p = 0; p < n.parts.size(); p++) {
        if (n.parts[p] >= id || !node(n.parts[p]).parts.empty() ||
            (p > 0 && n.parts[p - 1] >= n.parts[p])) {
          fail(id, "has parts that are not sorted leaf sets");
        }
      }
    }
  }
  for (const auto &[x, ys] : decl) {
    for (auto y : ys) {
      if (x >= size() || y >= size()) {
        fail(x, "is declared disjoint from a set that does not exist");
      } else if (subset(x, y) || subset(y, x)) {
        fail(x, "is declared disjoint from a set it is related to");
      }
    }
  }
  return ok;
}

bool AliasSetTable::disjoint(SetId a, SetId b) const {
  if (a == Unknown || b == Unknown) {
    return false;
  }
  for (auto pa : leaves_of(a)) {
    for (auto pb : leaves_of(b)) {
      if (!leaf_disjoint(pa, pb)) {
        return false;
      }
    }
  }
  return true;
}

bool AliasSetTable::disjoint_any(std::span<const SetId> a,
                                 std::span<const SetId> b) const {
  for (auto x : a) {
    for (auto y : b) {
      if (disjoint(x, y)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace foptim::optim
