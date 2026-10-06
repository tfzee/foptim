#pragma once
// The graph coloring part of the register allocator (RegAlloc2). Registers are
// assigned by solving a PBQP problem This file does not know
// about MIR so it can be tested on its own.
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"
#include "utils/arena.hpp"
#include "utils/map.hpp"
#include "utils/todo.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {
constexpr f32 spillCost = 1000.F;
constexpr f32 copyDiscount = -2;

constexpr CReg kAllocatableGPRegs[] = {
    CReg::A,  CReg::B,   CReg::C,   CReg::D,   CReg::DI,  CReg::SI,  CReg::R8,
    CReg::R9, CReg::R10, CReg::R11, CReg::R12, CReg::R13, CReg::R14, CReg::R15,
};

constexpr CReg kAllocatableVecRegs[] = {
    CReg::mm0,  CReg::mm1,  CReg::mm2,  CReg::mm3,  CReg::mm4,  CReg::mm5,
    CReg::mm6,  CReg::mm7,  CReg::mm8,  CReg::mm9,  CReg::mm10, CReg::mm11,
    CReg::mm12, CReg::mm13, CReg::mm14, CReg::mm15,
};
constexpr f32 infCost = std::numeric_limits<f32>::infinity();

// Register domains. A node can only ever end up in a small subset of all
// registers (a gpr vreg: spill + the allocatable gprs, a vec vreg: spill + the
// vec registers, precolored node), so costs and edge
// matrices are stored per domain. Within a domain the
// registers are sorted by their CReg value (0 = spill, the "register" that
// means the value lives on the stack), which keeps tie breaking independent of
// the storage. A domain is a bitmask of CRegs, to let a kind of node use
// additional registers (for example gpr values that can be parked in vec registers)
// add a mask here.
constexpr u32 kMaxDomain = 31;
constexpr u8 kNoLocal = 0xFF;

struct DomainKind {
  u8 size = 0;
  u8 regs[kMaxDomain] = {};
  // CReg -> index within the domain
  u8 local[numRegs] = {};
};

namespace kind {
enum : u8 {
  Gpr = 0,
  Vec = 1,
  // a gpr value that may also live in a vec register (not used yet)
  GprOrVec = 2,
  // followed by one kind per concrete register
  ConcreteBase = 3,
  N = ConcreteBase + numRegs - 1,
};
} // namespace kind

constexpr u64 reg_bit(CReg r) { return 1ULL << static_cast<u64>(r); }

constexpr u64 gpr_mask() {
  u64 m = reg_bit(CReg::Virtual);
  for (auto r : kAllocatableGPRegs) {
    m |= reg_bit(r);
  }
  return m;
}
constexpr u64 vec_mask() {
  u64 m = reg_bit(CReg::Virtual);
  for (auto r : kAllocatableVecRegs) {
    m |= reg_bit(r);
  }
  return m;
}

constexpr DomainKind make_domain(u64 mask) {
  DomainKind d;
  for (u8 r = 0; r < numRegs; r++) {
    d.local[r] = kNoLocal;
    if ((mask >> r) & 1) {
      d.local[r] = d.size;
      d.regs[d.size++] = r;
    }
  }
  return d;
}

constexpr std::array<DomainKind, kind::N> make_kinds() {
  std::array<DomainKind, kind::N> kinds{};
  kinds[kind::Gpr] = make_domain(gpr_mask());
  kinds[kind::Vec] = make_domain(vec_mask());
  kinds[kind::GprOrVec] = make_domain(gpr_mask() | vec_mask());
  for (u8 r = 1; r < numRegs; r++) {
    kinds[kind::ConcreteBase + r - 1] = make_domain(1ULL << r);
  }
  return kinds;
}
constexpr auto kKinds = make_kinds();

constexpr u8 kind_of(VReg reg) {
  if (reg.is_concrete()) {
    return static_cast<u8>(kind::ConcreteBase +
                           static_cast<u8>(reg.c_reg()) - 1);
  }
  return reg.is_vec_reg() ? kind::Vec : kind::Gpr;
}

constexpr bool kind_is_concrete(u8 k) { return k >= kind::ConcreteBase; }

constexpr u32 dom_size(u8 k) { return kKinds[k].size; }

// Cost of every register of the domain of a node, the spill "register" is
// always the first entry of a virtual registers domain.
struct CostVector {
  f32 cost[kMaxDomain] = {};
};

// Costs a node can have before any edge is looked at.
inline CostVector initial_costs(u8 k) {
  CostVector v;
  const auto &dom = kKinds[k];
  for (u32 i = 0; i < dom.size; i++) {
    v.cost[i] = dom.regs[i] == 0 ? spillCost : 0.F;
  }
  return v;
}

// Cost model. All costs are measured in memory operations executed, the
// currency of everything the allocator trades off: a spilled value costs a
// load or store per occu, a caller saved register costs a push/pop pair
// per call the value is alive across (inserted after allocation by the calling
// convention pass) and a callee saved register costs a push/pop pair in the
// prologue and epilogue of the function, which all values using the register
// share (hence cheaper than one pair per call).
constexpr f32 kMemOpCost = 1.F;
constexpr f32 kMinSpillCost = 1.F;
constexpr f32 kPushPopCost = 2.F;
constexpr f32 kCalleeSavedCost = 1.F;
// Values created by spilling (the reload temporaries) have a tiny live range,
// spilling them again can not help and would never terminate.
constexpr f32 kUnspillableCost = 1e6F;

struct NodeCostInfo {
  // summed block weights of every occurrence (use or def) of the register
  f32 use_weight = 0.F;
  // summed block weights of the calls the register is alive across
  f32 call_crossings = 0.F;
  bool unspillable = false;
};

// Sets the costs of the registers of a virtual register node (before edges).
inline void apply_cost_model(CostVector &v, u8 k, const NodeCostInfo &info,
                             const std::array<u8, numRegs> &save_class) {
  const auto &dom = kKinds[k];
  for (u32 l = 0; l < dom.size; l++) {
    const u8 reg = dom.regs[l];
    if (reg == 0) {
      v.cost[l] = info.unspillable
                      ? kUnspillableCost
                      : std::max(info.use_weight * kMemOpCost, kMinSpillCost);
      continue;
    }
    switch (save_class[reg]) {
    case CallerSaved:
      v.cost[l] += kPushPopCost * info.call_crossings;
      break;
    case CalleeSaved:
      v.cost[l] += kCalleeSavedCost;
      break;
    default:
      break;
    }
  }
}

// A dense (rows x cols) block of costs
using DenseCosts = f32[kMaxDomain][kMaxDomain];

// Edge cost matrix
// most edges are plain interference those are not stored and
// have cost == nullptr. Everything else points to arena memory that is only
// mutated while it is still exclusively owned by whoever created it (setup /
// R2 build a new matrix).
// The matrix is stored with the node with the lower index as rows
// (dom(lo) x dom(hi), row major).
struct CostMatrix {
  f32 *cost = nullptr;
  // if hard_cosntraint then we *cannot* ignore this matrix otherwise we can
  // disable it to make coloring possible
  bool hard_constraint = true;
  u8 kind_lo = 0;
  u8 kind_hi = 0;

  // cost of lo using lo_reg and hi using hi_reg
  [[nodiscard]] f32 get_cost(bool self_is_lo, u32 self_reg,
                             u32 other_reg) const {
    const u32 lo_reg = self_is_lo ? self_reg : other_reg;
    const u32 hi_reg = self_is_lo ? other_reg : self_reg;
    if (cost == nullptr) {
      return (lo_reg == hi_reg && lo_reg >= 1) ? infCost : 0.F;
    }
    const u8 lo = kKinds[kind_lo].local[lo_reg];
    const u8 hi = kKinds[kind_hi].local[hi_reg];
    ASSERT(lo != kNoLocal && hi != kNoLocal);
    return cost[lo * dom_size(kind_hi) + hi];
  }

  // fills out[row][col] where row is a register of `kind_row`, col one of
  // `kind_col`. row_is_lo says whether the row node is the lower node.
  void to_dense(bool row_is_lo, u8 kind_row, u8 kind_col,
                DenseCosts &out) const {
    const auto &rows = kKinds[kind_row];
    const auto &cols = kKinds[kind_col];
    if (cost == nullptr) {
      for (u32 r = 0; r < rows.size; r++) {
        for (u32 c = 0; c < cols.size; c++) {
          out[r][c] = (rows.regs[r] == cols.regs[c] && rows.regs[r] >= 1)
                          ? infCost
                          : 0.F;
        }
      }
      return;
    }
    if (row_is_lo) {
      for (u32 r = 0; r < rows.size; r++) {
        for (u32 c = 0; c < cols.size; c++) {
          out[r][c] = cost[r * cols.size + c];
        }
      }
    } else {
      for (u32 r = 0; r < rows.size; r++) {
        for (u32 c = 0; c < cols.size; c++) {
          out[r][c] = cost[c * rows.size + r];
        }
      }
    }
  }

  // moves a (rows x cols) block into arena memory so it can be shared. Rows
  // have to belong to the lower node.
  static f32 *store(const DenseCosts &dense, u32 rows, u32 cols) {
    auto *mem = utils::TempAlloc<f32>{}.allocate(rows * cols);
    for (u32 r = 0; r < rows; r++) {
      std::memcpy(mem + r * cols, dense[r], cols * sizeof(f32));
    }
    return mem;
  }
};

constexpr u32 kNoNode = static_cast<u32>(-1);

constexpr bool uid_is_concrete(u64 uid) {
  return uid + 1 < static_cast<u64>(CReg::N_REGS);
}

// the kind of a node that does not know its type yet (only a uid), the type of
// a virtual register is set once it is seen with one
constexpr u8 default_kind(u64 uid) {
  return uid_is_concrete(uid) ? kind_of(uid_to_reg(uid)) : kind::Gpr;
}

enum class ReductionType { R1, R2, RM };

// Everything a reduction needs to remember to pick the register of node
// once its neighbours are colored. The cost vector of node itself is not
// copied, a reduced node is detached from the graph so nobody changes
// `CostGraph::node_costs[node]` anymore.
struct StackRecord {
  ReductionType type = ReductionType::RM;
  u32 node = kNoNode;
  u32 neigh0 = kNoNode;
  // whether node is the lower node of the edge to neigh0 (the one that owns
  // the rows of the matrix)
  bool ab_self_lo = false;
  CostMatrix m_ab;

  // for r2
  u32 neigh1 = kNoNode;
  bool bc_self_lo = false;
  CostMatrix m_bc;

  // mrule
  struct BrokenEdge {
    u32 neighbor;
    CostMatrix matrix;
    bool flip;
  };
  TVec<BrokenEdge> broken_edges;
};

struct Adj {
  u32 other;
  u32 edge;
};

// Nodes are numbered densely in the order they are first touched, everything
// is indexed by that number instead of the (sparse) uid.
struct CostGraph {
  TVec<u32> uid_to_node;
  TVec<u64> node_uid;
  TVec<u8> node_kind;
  TVec<CostVector> node_costs;
  TVec<TVec<Adj>> neighbours;
  TVec<CostMatrix> edges;
  // (min node, max node) -> index into edges
  TMap<u64, u32> edge_lookup;
  // Nodes that are still part of the graph. Order matters since it decides
  // which node is reduced first, it behaves like the old node map: removing
  // moves the last element into the hole.
  TVec<u32> active;
  TVec<u32> active_pos;

  u32 node(u64 uid) {
    if (uid >= uid_to_node.size()) {
      uid_to_node.resize(uid + 1, kNoNode);
    }
    u32 &n = uid_to_node[uid];
    if (n == kNoNode) {
      n = static_cast<u32>(node_uid.size());
      node_uid.push_back(uid);
      node_kind.push_back(default_kind(uid));
      node_costs.emplace_back();
      neighbours.emplace_back();
      active_pos.push_back(static_cast<u32>(active.size()));
      active.push_back(n);
    }
    return n;
  }

  [[nodiscard]] bool has_node(u64 uid) const {
    return uid < uid_to_node.size() && uid_to_node[uid] != kNoNode;
  }

  [[nodiscard]] u32 n_nodes() const {
    return static_cast<u32>(node_uid.size());
  }

  [[nodiscard]] bool is_concrete(u32 n) const {
    return uid_is_concrete(node_uid[n]);
  }

  void set_kind(u32 n, u8 k) {
    node_kind[n] = k;
    node_costs[n] = initial_costs(k);
  }

  [[nodiscard]] static constexpr u64 edge_key(u32 a, u32 b) {
    ASSERT(a != b);
    return a < b ? (static_cast<u64>(b) << 32) | a
                 : (static_cast<u64>(a) << 32) | b;
  }

  [[nodiscard]] u32 find_edge(u32 a, u32 b) const {
    auto it = edge_lookup.find(edge_key(a, b));
    return it == edge_lookup.end() ? kNoNode : it->second;
  }

  [[nodiscard]] bool has_conn(u32 a, u32 b) const {
    return find_edge(a, b) != kNoNode;
  }

  u32 add_edge(u32 a, u32 b, CostMatrix m) {
    auto idx = static_cast<u32>(edges.size());
    edges.push_back(m);
    edge_lookup.emplace(edge_key(a, b), idx);
    neighbours[a].push_back({b, idx});
    neighbours[b].push_back({a, idx});
    return idx;
  }

  // Prefer that a and b end up in the same register. Adds an optional edge
  // (or discounts an existing one) that costs copyDiscount less where both
  // use the same register.
  void add_copy_discount(u32 a, u32 b) {
    u32 edge = find_edge(a, b);
    if (edge == kNoNode) {
      const bool a_is_lo = a < b;
      const u8 k_lo = node_kind[a_is_lo ? a : b];
      const u8 k_hi = node_kind[a_is_lo ? b : a];
      DenseCosts zero = {};
      edge = add_edge(a, b,
                      CostMatrix{.cost = CostMatrix::store(zero, dom_size(k_lo),
                                                           dom_size(k_hi)),
                                 .hard_constraint = false,
                                 .kind_lo = k_lo,
                                 .kind_hi = k_hi});
    }
    auto &m = edges[edge];
    // an implicit (interference) matrix is already infinite where both use the
    // same register so a discount changes nothing
    if (m.cost == nullptr) {
      return;
    }
    const auto &lo = kKinds[m.kind_lo];
    const auto &hi = kKinds[m.kind_hi];
    for (u32 l = 0; l < lo.size; l++) {
      for (u32 h = 0; h < hi.size; h++) {
        f32 &c = m.cost[l * hi.size + h];
        if (lo.regs[l] == hi.regs[h] && lo.regs[l] >= 1 && c != infCost) {
          c += copyDiscount;
        }
      }
    }
  }

  void remove_neighbour(u32 from, u32 to_remove) {
    auto &list = neighbours[from];
    list.erase(std::remove_if(list.begin(), list.end(),
                              [to_remove](Adj a) { return a.other == to_remove; }),
               list.end());
  }

  void erase_active(u32 pos) {
    u32 last = active.back();
    active.pop_back();
    if (pos < active.size()) {
      active[pos] = last;
      active_pos[last] = pos;
    }
  }

  void dump() {
    fmt::println("GRAPH {} NODES", active.size());
    for (u32 node : active) {
      fmt::print(" {} => ", uid_to_reg(node_uid[node]));
      for (auto n : neighbours[node]) {
        fmt::print(" {}, ", uid_to_reg(node_uid[n.other]));
      }
      fmt::println("");
    }
  }

  void clear() {
    uid_to_node.clear();
    node_uid.clear();
    node_kind.clear();
    node_costs.clear();
    neighbours.clear();
    edges.clear();
    edge_lookup.clear();
    active.clear();
    active_pos.clear();
  }
};

inline bool R1_Reduction(u32 node, CostGraph &graph,
                  TVec<StackRecord> &allocation_stack) {
  auto &neigh = graph.neighbours[node];
  if (neigh.size() != 1) {
    return false;
  }
  Adj n0 = neigh[0];
  const CostMatrix m_ab = graph.edges[n0.edge];
  const bool ab_flip = node < n0.other;
  const u8 k_a = graph.node_kind[node];
  const u8 k_b = graph.node_kind[n0.other];
  const u32 n_a = dom_size(k_a);
  const u32 n_b = dom_size(k_b);
  const auto &v_a = graph.node_costs[node].cost;

  // ab[reg of node][reg of neigh0]
  DenseCosts ab;
  m_ab.to_dense(ab_flip, k_a, k_b, ab);
  f32 add_vb[kMaxDomain];
  for (u32 i = 0; i < n_b; i++) {
    f32 new_cost = infCost;
    for (u32 j = 0; j < n_a; j++) {
      new_cost = std::min(new_cost, ab[j][i] + v_a[j]);
    }
    add_vb[i] = new_cost;
  }

  auto &record = allocation_stack.emplace_back();
  record.type = ReductionType::R1;
  record.node = node;
  record.neigh0 = n0.other;
  record.m_ab = m_ab;
  record.ab_self_lo = ab_flip;

  auto &v_b = graph.node_costs[n0.other].cost;
  for (u32 i = 0; i < n_b; i++) {
    v_b[i] += add_vb[i];
  }
  graph.remove_neighbour(n0.other, node);
  neigh.clear();
  return true;
}

inline bool R2_Reduction(u32 node, CostGraph &graph,
                  TVec<StackRecord> &allocation_stack) {
  auto &neigh = graph.neighbours[node];
  if (neigh.size() != 2) {
    return false;
  }
  // a - M_ab - b - M_bc - c
  //  -->
  //  a - M_ac - c and b is pushed
  Adj n0 = neigh[0];
  Adj n1 = neigh[1];

  const u32 neigh0 = n0.other;
  const u32 neigh1 = n1.other;
  u32 ac_edge = graph.find_edge(neigh0, neigh1);
  const bool is_already_connected = ac_edge != kNoNode;
  const u8 k_a = graph.node_kind[neigh0];
  const u8 k_b = graph.node_kind[node];
  const u8 k_c = graph.node_kind[neigh1];
  const u32 n_a = dom_size(k_a);
  const u32 n_b = dom_size(k_b);
  const u32 n_c = dom_size(k_c);
  const bool ab_needs_flip = neigh0 < node;
  const bool bc_needs_flip = node < neigh1;
  const bool ac_needs_flip = neigh0 < neigh1;

  // ac[reg of a][reg of c]
  DenseCosts ac = {};
  bool new_ac_hard = false;
  // if there already is a connection between a and c we add to it, otherwise
  // we start from nothing and only use the old ab bc connections
  if (is_already_connected) {
    const auto &old_ac = graph.edges[ac_edge];
    old_ac.to_dense(ac_needs_flip, k_a, k_c, ac);
    new_ac_hard = old_ac.hard_constraint;
  }
  const CostMatrix m_ab = graph.edges[n0.edge];
  const CostMatrix m_bc = graph.edges[n1.edge];
  // TODO: double check this
  new_ac_hard = new_ac_hard || (m_ab.hard_constraint && m_bc.hard_constraint);
  const auto &v_b = graph.node_costs[node].cost;

  // ab[reg of a][reg of b], bc[reg of b][reg of c]
  DenseCosts ab;
  DenseCosts bc;
  m_ab.to_dense(ab_needs_flip, k_a, k_b, ab);
  m_bc.to_dense(bc_needs_flip, k_b, k_c, bc);
  for (u32 i = 0; i < n_a; i++) {
    f32 ab_v[kMaxDomain];
    for (u32 j = 0; j < n_b; j++) {
      ab_v[j] = ab[i][j] + v_b[j];
    }
    for (u32 k = 0; k < n_c; k++) {
      f32 curr_cost = infCost;
      for (u32 j = 0; j < n_b; j++) {
        curr_cost = std::min(curr_cost, ab_v[j] + bc[j][k]);
      }
      ac[i][k] += curr_cost;
    }
  }

  auto &record = allocation_stack.emplace_back();
  record.type = ReductionType::R2;
  record.node = node;
  record.neigh0 = neigh0;
  record.neigh1 = neigh1;
  record.m_ab = m_ab;
  record.m_bc = m_bc;
  record.ab_self_lo = node < neigh0;
  record.bc_self_lo = bc_needs_flip;

  // rows belong to the lower node
  CostMatrix new_ac;
  new_ac.hard_constraint = new_ac_hard;
  if (ac_needs_flip) {
    new_ac.kind_lo = k_a;
    new_ac.kind_hi = k_c;
    new_ac.cost = CostMatrix::store(ac, n_a, n_c);
  } else {
    DenseCosts transposed;
    for (u32 i = 0; i < n_a; i++) {
      for (u32 k = 0; k < n_c; k++) {
        transposed[k][i] = ac[i][k];
      }
    }
    new_ac.kind_lo = k_c;
    new_ac.kind_hi = k_a;
    new_ac.cost = CostMatrix::store(transposed, n_c, n_a);
  }
  if (is_already_connected) {
    graph.edges[ac_edge] = new_ac;
  } else {
    graph.add_edge(neigh0, neigh1, new_ac);
  }
  graph.remove_neighbour(neigh0, node);
  graph.remove_neighbour(neigh1, node);
  graph.neighbours[node].clear();
  return true;
}

inline bool RM_Reduction(CostGraph &graph, TVec<StackRecord> &allocation_stack) {
  u32 victim_node = kNoNode;
  f32 min_spill_metric = std::numeric_limits<f32>::max();

  // Heuristic Selection: Find the best node to eject from graph to maybe spill
  for (u32 node : graph.active) {
    const auto &neigh = graph.neighbours[node];
    // precolored nodes can never be spilled
    if (neigh.empty() || graph.is_concrete(node)) {
      continue;
    }

    // TODO: precalculate spill weights
    f32 spill_weight = graph.node_costs[node].cost[0];
    f32 degree = static_cast<f32>(neigh.size());

    // Chaitin style heuristic: minimize weight/degree
    f32 metric = spill_weight / degree;
    if (metric < min_spill_metric) {
      min_spill_metric = metric;
      victim_node = node;
    }
  }

  // If no nodes are left, the graph is solved/empty!
  if (victim_node == kNoNode) {
    return false;
  }
  auto &neigh_list = graph.neighbours[victim_node];

  auto &record = allocation_stack.emplace_back();
  record.type = ReductionType::RM;
  record.node = victim_node;
  record.broken_edges.reserve(neigh_list.size());

  for (Adj adj : neigh_list) {
    StackRecord::BrokenEdge broken;
    broken.neighbor = adj.other;
    broken.matrix = graph.edges[adj.edge];
    broken.flip = victim_node < adj.other;
    record.broken_edges.push_back(broken);

    graph.remove_neighbour(adj.other, victim_node);
  }
  neigh_list.clear();
  graph.erase_active(graph.active_pos[victim_node]);
  return true;
}

inline void minimize_graph(CostGraph &graph, TVec<StackRecord> &allocation_stack) {
  // at most one record per node
  allocation_stack.reserve(graph.n_nodes());
  while (true) {
    bool found_low_degree_node = false;
    bool found_any_virtual = false;
    // TODO: mazbe should first do all 1degree then all 2degree nodes
    // 1 degree nodes i then oculd also run in parralel?
    u32 pos = 0;
    while (pos < graph.active.size()) {
      u32 node = graph.active[pos];
      if (graph.is_concrete(node)) {
        ++pos;
        continue;
      }
      found_any_virtual = true;
      if (R1_Reduction(node, graph, allocation_stack) ||
          R2_Reduction(node, graph, allocation_stack)) {
        // the last node moved into this slot, so do not advance
        graph.erase_active(pos);
        found_low_degree_node = true;
      } else {
        ++pos;
      }
    }

    // If we processed low-degree nodes, loop again to see if the reductions
    // created *new* low-degree nodes.
    if (found_low_degree_node) {
      continue;
    }
    // NOTE: dropping the optional (copy discount) edges of nodes with at most
    // two hard neighbours before resorting to RM was tried and made the static
    // metrics worse (more movs, more instructions), see rejected-experiments.md.

    if (graph.active.empty() || !found_any_virtual) {
      break;
    }

    // Spill / Minimum-degree reduction step if no degree 1 or 2 nodes remain
    if (!RM_Reduction(graph, allocation_stack)) {
      break;
    }
  }

  for (u32 node : graph.active) {
    if (graph.is_concrete(node)) {
      continue;
    }
    ASSERT(graph.neighbours[node].empty());
    auto &record = allocation_stack.emplace_back();
    record.node = node;
    record.type = ReductionType::RM;
  }
}

// assignments is indexed by node, nodes that have not been colored yet and
// spilled nodes are CReg::Virtual
inline void actually_allocate(const CostGraph &graph,
                       TVec<StackRecord> &allocation_stack,
                       TVec<CReg> &assignments, TVec<u64> &needs_spilling) {
  assignments.assign(graph.n_nodes(), CReg::Virtual);
  for (u32 node = 0; node < graph.n_nodes(); node++) {
    if (graph.is_concrete(node)) {
      assignments[node] = uid_to_reg(graph.node_uid[node]).c_reg();
    }
  }

  while (!allocation_stack.empty()) {
    const StackRecord &record = allocation_stack.back();

    u32 u = record.node;
    const auto &dom = kKinds[graph.node_kind[u]];
    f32 selection_vector[kMaxDomain] = {};

    for (u32 i = 0; i < dom.size; i++) {
      selection_vector[i] = graph.node_costs[u].cost[i];
    }

    switch (record.type) {
    case ReductionType::R1: {
      auto assigned_reg_neigh0 = assignments[record.neigh0];

      for (u32 i = 0; i < dom.size; i++) {
        // just go baesd on what the neighbour took
        selection_vector[i] += record.m_ab.get_cost(
            record.ab_self_lo, dom.regs[i], static_cast<u32>(assigned_reg_neigh0));
      }
    } break;
    case ReductionType::R2: {
      auto assigned_reg_neigh0 = assignments[record.neigh0];
      auto assigned_reg_neigh1 = assignments[record.neigh1];

      for (u32 i = 0; i < dom.size; i++) {
        // Add interference cost from neighbor 0
        f32 cost_a = record.m_ab.get_cost(
            record.ab_self_lo, dom.regs[i], static_cast<u32>(assigned_reg_neigh0));
        // Add interference cost from neighbor 1
        f32 cost_b = record.m_bc.get_cost(
            record.bc_self_lo, dom.regs[i], static_cast<u32>(assigned_reg_neigh1));

        selection_vector[i] += (cost_a + cost_b);
      }
    } break;
    case ReductionType::RM: {
      // evaluate every enighbour edge we broke during the RM phase
      for (const auto &broken : record.broken_edges) {
        auto assigned_reg_neigh = assignments[broken.neighbor];

        for (u32 i = 0; i < dom.size; i++) {
          selection_vector[i] += broken.matrix.get_cost(
              broken.flip, dom.regs[i], static_cast<u32>(assigned_reg_neigh));
        }
      }
    } break;
    }

    // chose the best :)
    u32 best_choice = 0;
    // needs to be lower then INF since inf should never be chosen
    f32 min_cost = 1e9F;

    // the domain is sorted by register so ties behave like a scan over all
    for (u32 l = 0; l < dom.size; l++) {
      const u32 i = dom.regs[l];
      if (selection_vector[l] < min_cost) {
        min_cost = selection_vector[l];
        best_choice = i;
      } else if (selection_vector[l] == min_cost && best_choice == 0 &&
                 i != 0) {
        // Prefer physical register over Spill on tie
        best_choice = i;
      }
    }
    auto best_creg = static_cast<CReg>(best_choice);
    if (best_creg == CReg::Virtual) {
      needs_spilling.push_back(graph.node_uid[u]);
    }
    assignments[u] = best_creg;
    allocation_stack.pop_back();
  }
}

} // namespace foptim::fmir
