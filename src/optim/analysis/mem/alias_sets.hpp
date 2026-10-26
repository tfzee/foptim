#pragma once
#include <optional>
#include <span>

#include "utils/map.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

using SetId = u32;

/*
  A set is a group of memory locations a pointer may point into. Relations:
  subset (a set may have several parents), disjoint (declared between two sets,
  inherited by subsets) and group (the object sets of one root are pairwise
  disjoint without n^2 declarations). Everything else may alias. A query only
  looks at the ancestor lists of the two sets and their declared pairs, it never
  walks the graph.

  A union (phi or select of pointers) is a hash consed set of leaf sets. Parts
  inside another part are dropped, more than MaxParts parts are widened to
  their deepest common ancestor. A is a subset of union(A, B), the union is a
  subset of / disjoint from X if all parts are.

  The roots are shared (roots()), everything else lives in an overlay of one
  function, so parallel function passes never write shared state.
*/
class AliasSetTable {
 public:
  // superset of everything, never disjoint from anything
  static constexpr SetId Unknown = 0;
  // everything but the stack objects that never escape
  static constexpr SetId Foreign = 1;
  static constexpr SetId Null = 2;
  static constexpr SetId Stack = 3;
  static constexpr SetId Globals = 4;
  static constexpr SetId Heap = 5;
  static constexpr SetId ReadOnlyGlobals = 6;
  // pointer arguments, not the own stack, noalias memory or fresh allocations
  static constexpr SetId Incoming = 7;
  static constexpr SetId IncomingNonNull = 8;
  // the memory behind noalias arguments (one object each), may be null
  static constexpr SetId Restrict = 9;
  static constexpr SetId NumRoots = 10;

  static constexpr u32 MaxParts = 4;

  static const AliasSetTable &roots();

  // an overlay on top of the roots (or of any other table)
  explicit AliasSetTable(const AliasSetTable &parent);

  // a new set that is a subset of all parents
  SetId create(std::span<const SetId> parents);
  // a new object set under a root, disjoint from all other objects under it
  SetId create_object(SetId root, std::span<const SetId> extra_parents = {});
  void declare_disjoint(SetId a, SetId b);
  SetId union_of(SetId a, SetId b);

  [[nodiscard]] bool subset(SetId a, SetId b) const;
  [[nodiscard]] bool disjoint(SetId a, SetId b) const;
  // an access in several sets lies in their intersection
  [[nodiscard]] bool disjoint_any(std::span<const SetId> a,
                                  std::span<const SetId> b) const;
  // checks the sets of this table (not its parent), prints what is wrong
  [[nodiscard]] bool verify() const;
  [[nodiscard]] bool is_union(SetId s) const { return !node(s).parts.empty(); }
  [[nodiscard]] std::span<const SetId> parts(SetId s) const;
  // an overlay is only valid while its parent has this many sets
  [[nodiscard]] u32 parent_size() const { return base; }
  [[nodiscard]] u32 size() const {
    return base + static_cast<u32>(nodes.size());
  }

 private:
  struct Node {
    // the set itself and every set it is a subset of, Unknown included
    FVec<SetId> anc;
    // the leaf sets of a union, empty for a leaf
    FVec<SetId> parts;
    // object sets with the same group are pairwise disjoint, 0 = none
    SetId group = 0;
  };
  struct RootsTag {};
  explicit AliasSetTable(RootsTag);

  [[nodiscard]] const Node &node(SetId s) const;
  [[nodiscard]] const FVec<SetId> *declared(SetId s) const;
  [[nodiscard]] bool leaf_disjoint(SetId a, SetId b) const;
  [[nodiscard]] bool leaf_subset(SetId a, SetId b) const {
    return a == b || has(node(a).anc, b);
  }
  [[nodiscard]] static bool has(const FVec<SetId> &v, SetId s);
  [[nodiscard]] std::optional<SetId> find_union(const FVec<SetId> &parts,
                                                u64 hash) const;
  [[nodiscard]] FVec<SetId> leaves_of(SetId s) const;
  [[nodiscard]] SetId widen(const FVec<SetId> &parts) const;
  SetId add_node(Node n);

  const AliasSetTable *parent = nullptr;
  u32 base = 0;
  FVec<Node> nodes;
  // declared disjoint pairs, both directions. An overlay also has entries for
  // sets of its parent.
  FDMap<SetId, FVec<SetId>> decl;
  FDMap<u64, FVec<SetId>> unions;
};

}  // namespace foptim::optim
