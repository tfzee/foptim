#include "reg_alloc.hpp"

#include <algorithm>
#include <ankerl/unordered_dense.h>
#include <cstring>
#include <fmt/base.h>
#include <fmt/ranges.h>
#include <limits>
#include <ranges>

#include "mir/analysis/cfg.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"
#include "mir/optim/reg_alloc.hpp"
#include "utils/set.hpp"
#include "utils/todo.hpp"
#include "utils/types.hpp"

namespace foptim::fmir {
void replace_vargs(IRVec<MBB> &bbs, const TMap<u64, CReg> &reg_mapping) {
  for (auto &bb : bbs) {
    for (auto &instr : bb.instrs) {
      replace_vargs(instr, reg_mapping);
    }
  }
}

void replace_vargs(MInstr &instr, const TMap<u64, CReg> &reg_mapping) {
  for (u32 i = 0; i < instr.n_args; i++) {
    switch (instr.args[i].type) {
    case MArgument::ArgumentType::StackSlot:
    case MArgument::ArgumentType::Imm:
    case MArgument::ArgumentType::Label:
    case MArgument::ArgumentType::MemLabel:
    case MArgument::ArgumentType::MemImmLabel:
    case MArgument::ArgumentType::MemImm:
      break;
    case MArgument::ArgumentType::VReg:
    case MArgument::ArgumentType::MemImmVReg:
    case MArgument::ArgumentType::MemVReg: {
      auto reg = instr.args[i].reg;
      if (!reg.is_concrete() && reg_mapping.contains(reg.virt_id())) {
        instr.args[i].reg.rty = VReg::RegType::Concrete;
        instr.args[i].reg.conc.creg = reg_mapping.at(reg.virt_id());
      }
      break;
    }
    case MArgument::ArgumentType::MemVRegVRegScale:
    case MArgument::ArgumentType::MemImmVRegVReg:
    case MArgument::ArgumentType::MemVRegVReg:
    case MArgument::ArgumentType::MemImmVRegVRegScale: {
      auto reg = instr.args[i].reg;
      auto indx = instr.args[i].indx;
      if (!reg.is_concrete() && reg_mapping.contains(reg.virt_id())) {
        instr.args[i].reg.rty = VReg::RegType::Concrete;
        instr.args[i].reg.conc.creg = reg_mapping.at(reg.virt_id());
      }
      if (!indx.is_concrete() && reg_mapping.contains(indx.virt_id())) {
        instr.args[i].indx.rty = VReg::RegType::Concrete;
        instr.args[i].indx.conc.creg = reg_mapping.at(indx.virt_id());
      }
      break;
    }
    case MArgument::ArgumentType::MemLabelVregScale:
    case MArgument::ArgumentType::MemLabelVreg:
    case MArgument::ArgumentType::MemImmVRegScale: {
      auto indx = instr.args[i].indx;
      if (!indx.is_concrete() && reg_mapping.contains(indx.virt_id())) {
        instr.args[i].indx.rty = VReg::RegType::Concrete;
        instr.args[i].indx.conc.creg = reg_mapping.at(indx.virt_id());
      }
      break;
    }
    }
  }
}

void replace_varg(MInstr &instr, u64 from, VReg to, bool keep_type) {
  const auto replace = [from, to, keep_type](VReg &reg) -> void {
    if (!reg.is_concrete() && from == reg.virt_id()) {
      if (keep_type) {
        auto orig_type = reg.ty;
        reg = to;
        reg.ty = orig_type;
      } else {
        reg = to;
      }
    }
  };

  for (u32 i = 0; i < instr.n_args; i++) {
    switch (instr.args[i].type) {
    case MArgument::ArgumentType::StackSlot:
    case MArgument::ArgumentType::Imm:
    case MArgument::ArgumentType::Label:
    case MArgument::ArgumentType::MemLabel:
    case MArgument::ArgumentType::MemImmLabel:
    case MArgument::ArgumentType::MemImm:
      break;
    case MArgument::ArgumentType::VReg:
    case MArgument::ArgumentType::MemImmVReg:
    case MArgument::ArgumentType::MemVReg: {
      replace(instr.args[i].reg);
      break;
    }
    case MArgument::ArgumentType::MemVRegVRegScale:
    case MArgument::ArgumentType::MemImmVRegVReg:
    case MArgument::ArgumentType::MemVRegVReg:
    case MArgument::ArgumentType::MemImmVRegVRegScale: {
      replace(instr.args[i].reg);
      replace(instr.args[i].indx);
      break;
    }
    case MArgument::ArgumentType::MemLabelVregScale:
    case MArgument::ArgumentType::MemLabelVreg:
    case MArgument::ArgumentType::MemImmVRegScale: {
      replace(instr.args[i].indx);
      break;
    }
    }
  }
}

namespace {
constexpr f32 spillCost = 1000.F;
constexpr f32 copyDiscount = -2;

constexpr CReg kAllocatableGPRegs[] = {
    CReg::A,  CReg::B,   CReg::C,   CReg::D,   CReg::DI,  CReg::SI,  CReg::R8,
    CReg::R9, CReg::R10, CReg::R11, CReg::R12, CReg::R13, CReg::R14, CReg::R15,
};
// constexpr size_t kNumAllocatableGPR =
//     sizeof(kAllocatableGPRegs) / sizeof(kAllocatableGPRegs[0]);

constexpr CReg kAllocatableVecRegs[] = {
    CReg::mm0,  CReg::mm1,  CReg::mm2,  CReg::mm3,  CReg::mm4,  CReg::mm5,
    CReg::mm6,  CReg::mm7,  CReg::mm8,  CReg::mm9,  CReg::mm10, CReg::mm11,
    CReg::mm12, CReg::mm13, CReg::mm14, CReg::mm15,
};
// constexpr size_t kNumAllocatableVec =
//     sizeof(kAllocatableVecRegs) / sizeof(kAllocatableVecRegs[0]);
constexpr size_t numRegs = static_cast<u64>(CReg::N_REGS);
constexpr f32 infCost = std::numeric_limits<f32>::infinity();

struct CostVector {
  f32 cost[numRegs] = {};

  [[nodiscard]] constexpr f32 get_spill_cost() const { return cost[0]; }
  void dump() const {
    fmt::print("V{{");
    for (float i : cost) {
      fmt::print("{}, ", i);
    }
    fmt::print("}}");
  }
};

struct CostMatrix {
  f32 cost[numRegs][numRegs] = {};
  // if hard_cosntraint then we *cannot* ignore this matrix otherwise we can
  // disable it to make coloring possible
  bool hard_constraint = true;

  [[nodiscard]] constexpr f32 get_cost(bool flip, u32 row, u32 col) const {
    if (flip) {
      return cost[row][col];
    }
    return cost[col][row];
  };

  void dump() const {
    for (const auto &i : cost) {
      for (float j : i) {
        fmt::print("{}, ", j);
      }
      fmt::println(";");
    }
  }
};

struct EdgeId {
  u64 a;
  u64 b;

  constexpr EdgeId(u64 i1, u64 i2) {
    ASSERT(i1 != i2);
    if (i1 >= i2) {
      a = i1;
      b = i2;
    } else {
      a = i2;
      b = i1;
    }
  }

  [[nodiscard]] constexpr bool operator==(EdgeId o) const {
    return a == o.a && b == o.b;
  }
};
} // namespace
} // namespace foptim::fmir

template <> struct ankerl::unordered_dense::hash<foptim::fmir::EdgeId> {
  using is_avalanching = void;

  [[nodiscard]] auto operator()(foptim::fmir::EdgeId const &x) const noexcept
      -> uint64_t {
    return ankerl::unordered_dense::detail::wyhash::mix(
        ankerl::unordered_dense::detail::wyhash::hash(x.a),
        ankerl::unordered_dense::detail::wyhash::hash(x.b));
  }
};

namespace foptim::fmir {
namespace {
enum class ReductionType { R1, R2, RM };

struct StackRecord {
  ReductionType type;
  u32 node;
  TVec<f32> original_v;
  u32 neigh0;
  CostMatrix m_ab;
  bool ab_flip;

  // for r2
  u32 neigh1;
  CostMatrix m_bc;
  bool bc_flip;

  // mrule
  struct BrokenEdge {
    u32 neighbor;
    CostMatrix matrix;
    bool flip;
  };
  std::vector<BrokenEdge> broken_edges;
};

struct CostGraph {
  // the 0 intex is used to mark spilling since its not a valid CReg
  TMap<u64, CostVector> node_costs;
  TMap<EdgeId, CostMatrix> connection_costs;
  TMap<u64, TVec<u64>> neighbours;

  [[nodiscard]] constexpr bool has_conn(u64 a, u64 b) const {
    return connection_costs.contains(EdgeId(a, b));
  }

  void dump() {
    fmt::println("GRAPH {} NODES", neighbours.size());
    for (auto [node, neigh] : neighbours) {
      fmt::print(" {} => ", uid_to_reg(node));
      for (auto n : neigh) {
        fmt::print(" {}, ", uid_to_reg(n));
      }
      fmt::println("");
    }
  }

  void clear() {
    node_costs.clear();
    connection_costs.clear();
    neighbours.clear();
  }
};

void set_default_vec_reg_costs(VReg reg, CostVector &v) {
  (void)reg;
  // could make for f64 sized types gpr legal but expensive
  //  for (auto r : kGPRRegs) {
  //    v.cost[static_cast<u64>(r)] = 0;
  //  }
  for (auto r : kAllocatableVecRegs) {
    v.cost[static_cast<u64>(r)] = 0;
  }
}

void set_default_gpr_reg_costs(VReg reg, CostVector &v) {
  (void)reg;
  // could make for f64 sized types gpr legal but expensive
  // for (auto r : kVecRegs) {
  //   v.cost[static_cast<u64>(r)] = 0;
  // }
  for (auto r : kAllocatableGPRegs) {
    v.cost[static_cast<u64>(r)] = 0;
  }
}

void set_default_reg_costs(VReg reg, CostVector &vec) {
  for (size_t i = 1; i < numRegs; i++) {
    vec.cost[static_cast<u64>(i)] = infCost;
  }
  // TODO: spill costs
  vec.cost[0] = reg.is_concrete() ? infCost : spillCost;
  if (reg.is_concrete()) {
    vec.cost[static_cast<u64>(reg.c_reg())] =
        -std::numeric_limits<f32>::infinity();
  } else {
    if (reg.is_vec_reg()) {
      set_default_vec_reg_costs(reg, vec);
    } else {
      set_default_gpr_reg_costs(reg, vec);
    }
  }
}

bool R1_Reduction(u64 node, TVec<u64> &neigh, CostGraph &graph,
                  TVec<StackRecord> &allocation_stack, bool ignore_optional) {
  if (neigh.size() == 0) {
    return false;
  }
  u64 neigh0 = neigh[0];
  if (ignore_optional) {
    bool found_hard_neigh = false;
    for (auto n : neigh) {
      auto edge_ab = EdgeId{node, n};
      if (graph.connection_costs[edge_ab].hard_constraint) {
        if (found_hard_neigh) {
          return false;
        }
        found_hard_neigh = true;
        neigh0 = n;
      }
    }
    if (!found_hard_neigh) {
      return false;
    }
  } else if (neigh.size() != 1) {
    return false;
  }
  f32 add_vb[numRegs] = {};
  auto edge_ab = EdgeId{node, neigh0};
  const auto &m_ab = graph.connection_costs[edge_ab];
  const auto &v_a = graph.node_costs[node].cost;
  bool ab_flip = node < neigh0;

  for (u32 i = 0; i < numRegs; i++) {
    f32 new_cost = infCost;
    for (u32 j = 0; j < numRegs; j++) {
      new_cost = std::min(new_cost, m_ab.get_cost(ab_flip, j, i) + v_a[j]);
    }
    add_vb[i] = new_cost;
  }

  StackRecord record;
  record.type = ReductionType::R1;
  record.node = node;
  record.original_v = TVec<f32>(std::begin(v_a), std::end(v_a));
  record.neigh0 = neigh0;
  record.m_ab = m_ab;
  record.ab_flip = ab_flip;
  allocation_stack.push_back(record);

  for (u32 i = 0; i < numRegs; i++) {
    graph.node_costs[neigh0].cost[i] += add_vb[i];
  }
  // graph.node_costs[neigh[0]].dump();
  // fmt::println("");
  auto &neigh0_neigh = graph.neighbours[neigh0];
  neigh0_neigh.erase(
      std::remove(neigh0_neigh.begin(), neigh0_neigh.end(), node),
      neigh0_neigh.end());
  neigh.clear();
  return true;
}

bool R2_Reduction(u64 node, TVec<u64> &neigh, CostGraph &graph,
                  TVec<StackRecord> &allocation_stack, bool ignore_optional) {
  if (neigh.size() < 2) {
    return false;
  }
  // a - M_ab - b - M_bc - c
  //  -->
  //  a - M_ac - c and b is pushed
  u64 neigh0 = neigh[0];
  u64 neigh1 = neigh[1];
  auto edge_ac = EdgeId{neigh0, neigh1};

  if (ignore_optional) {
    u32 found_hard_neigh = 0;
    for (auto n : neigh) {
      auto edge_ab = EdgeId{node, n};
      if (graph.connection_costs[edge_ab].hard_constraint) {
        if (found_hard_neigh == 0) {
          found_hard_neigh = 1;
          neigh0 = n;
        } else if (found_hard_neigh == 1) {
          found_hard_neigh = 2;
          neigh1 = n;
        } else {
          return false;
        }
      }
    }
    if (found_hard_neigh != 2) {
      return false;
    }
  } else if (neigh.size() != 2) {
    return false;
  }
  auto edge_ab = EdgeId{neigh0, node};
  auto edge_bc = EdgeId{node, neigh1};
  bool is_already_connected = false;
  CostMatrix new_ac{};
  new_ac.hard_constraint = false;
  // we set it to false above so if it gets overwritten here we can recognize
  // that and then use that forced value otherwise we can use the default false
  // to only chekc theo old ab bc connections
  if (graph.has_conn(neigh0, neigh1)) {
    // might make sense to not copy it here
    is_already_connected = true;
    new_ac = graph.connection_costs[edge_ac];
  }
  const auto &m_ab = graph.connection_costs[edge_ab];
  const auto &m_bc = graph.connection_costs[edge_bc];
  // TODO: double check this
  new_ac.hard_constraint =
      new_ac.hard_constraint || (m_ab.hard_constraint && m_bc.hard_constraint);
  const auto &v_b = graph.node_costs[node].cost;

  const bool ab_needs_flip = neigh0 < node;
  const bool bc_needs_flip = node < neigh1;
  const bool ac_needs_flip = neigh0 < neigh1;
  for (u32 i = 0; i < numRegs; i++) {
    for (u32 k = 0; k < numRegs; k++) {
      f32 curr_cost = infCost;
      for (u32 j = 0; j < numRegs; j++) {
        curr_cost =
            std::min(curr_cost, m_ab.get_cost(ab_needs_flip, i, j) + v_b[j] +
                                    m_bc.get_cost(bc_needs_flip, j, k));
      }
      if (ac_needs_flip) {
        new_ac.cost[i][k] += curr_cost;
      } else {
        new_ac.cost[k][i] += curr_cost;
      }
    }
  }
  // fmt::println("\n===AB===");
  // m_ab.dump();
  // fmt::println("\n===BC===");
  // m_bc.dump();
  // // fmt::println("===Bv===");
  // // v_b.dump();
  // fmt::println("\n===new===");
  // new_ac.dump();
  // fmt::println("");

  StackRecord record;
  record.type = ReductionType::R2;
  record.node = node;
  record.original_v = TVec<f32>(std::begin(v_b), std::end(v_b));
  record.neigh0 = neigh0;
  record.neigh1 = neigh1;
  record.m_ab = m_ab;
  record.m_bc = m_bc;
  record.ab_flip = ab_needs_flip;
  record.bc_flip = bc_needs_flip;
  allocation_stack.push_back(record);

  graph.connection_costs[edge_ac] = new_ac;
  if (!is_already_connected) {
    graph.neighbours[neigh0].push_back(neigh1);
    graph.neighbours[neigh1].push_back(neigh0);
  }
  auto &neigh0_neigh = graph.neighbours[neigh0];
  neigh0_neigh.erase(
      std::remove(neigh0_neigh.begin(), neigh0_neigh.end(), node),
      neigh0_neigh.end());
  auto &neigh1_neigh = graph.neighbours[neigh1];
  neigh1_neigh.erase(
      std::remove(neigh1_neigh.begin(), neigh1_neigh.end(), node),
      neigh1_neigh.end());
  neigh.clear();
  return true;
}

bool RM_Reduction(CostGraph &graph, TVec<StackRecord> &allocation_stack) {
  u32 victim_node = static_cast<u32>(-1);
  f32 min_spill_metric = std::numeric_limits<f32>::max();

  // Heuristic Selection: Find the best node to eject from graph to maybe spill
  for (const auto &[node, neigh] : graph.neighbours) {
    if (neigh.empty()) {
      continue;
    }

    // TODO: precalculate spill weights
    f32 spill_weight = graph.node_costs[node].get_spill_cost();
    f32 degree = static_cast<f32>(neigh.size());

    // Standard Chaitin heuristic: minimize weight/degree
    f32 metric = spill_weight / degree;
    if (metric < min_spill_metric) {
      min_spill_metric = metric;
      victim_node = node;
    }
  }

  // If no nodes are left, the graph is solved/empty!
  if (victim_node == static_cast<u32>(-1)) {
    return false;
  }
  auto &neigh_list = graph.neighbours[victim_node];
  const auto &v_v = graph.node_costs[victim_node].cost;

  StackRecord record;
  record.type = ReductionType::RM;
  record.node = victim_node;
  record.original_v = TVec<f32>(std::begin(v_v), std::end(v_v));

  for (u32 neighbor_id : neigh_list) {
    auto edge_id = EdgeId{victim_node, neighbor_id};
    bool needs_flip = victim_node < neighbor_id;

    StackRecord::BrokenEdge broken;
    broken.neighbor = neighbor_id;
    broken.matrix = graph.connection_costs[edge_id];
    broken.flip = needs_flip;
    record.broken_edges.push_back(broken);

    auto &r_neigh = graph.neighbours[neighbor_id];
    r_neigh.erase(std::remove(r_neigh.begin(), r_neigh.end(), victim_node),
                  r_neigh.end());
  }
  allocation_stack.push_back(record);
  neigh_list.clear();
  graph.neighbours.erase(victim_node);
  return true;
}

void setup_costs(const MFunc &func, CostGraph &graph,
                 const TMap<VReg, TSet<size_t>> &lifetimes) {
  for (const auto &[reg, coll] : lifetimes) {
    auto uid = reg_to_uid(reg);
    CostVector vec;
    for (size_t i = 1; i < numRegs; i++) {
      vec.cost[static_cast<u64>(i)] = infCost;
    }
    // TODO: spill costs
    vec.cost[0] = reg.is_concrete() ? infCost : spillCost;
    if (reg.is_concrete()) {
      vec.cost[static_cast<u64>(reg.c_reg())] = 0;
    } else {
      if (reg.is_vec_reg()) {
        set_default_vec_reg_costs(reg, vec);
      } else {
        set_default_gpr_reg_costs(reg, vec);
      }
    }
    graph.node_costs[uid] = vec;
    graph.neighbours[uid];

    // fmt::println("Reg {}", reg);
    for (auto c : coll) {
      if (c != uid) {
        // fmt::println("  coll {} {}    {} {}", c, uid, reg, uid_to_reg(c));
        CostMatrix matrix;
        for (size_t i = 1; i < numRegs; i++) {
          matrix.cost[static_cast<u64>(i)][static_cast<u64>(i)] = infCost;
        }
        EdgeId e = EdgeId(uid, c);
        if (!graph.connection_costs.contains(e)) {
          graph.connection_costs[e] = matrix;
          graph.neighbours[uid].push_back(c);
          graph.neighbours[c].push_back(uid);
        }
      }
    }
  }

  // ensure every vreg is represented even the ones without collisions and
  // make cost reductions
  for (const auto &bb : func.bbs) {
    for (const auto &instr : bb.instrs) {
      for (size_t i_arg = 0; i_arg < instr.n_args; i_arg++) {
        switch (instr.args[i_arg].type) {
        case MArgument::ArgumentType::StackSlot:
        case MArgument::ArgumentType::Imm:
        case MArgument::ArgumentType::Label:
        case MArgument::ArgumentType::MemLabel:
        case MArgument::ArgumentType::MemImmLabel:
        case MArgument::ArgumentType::MemImm:
          break;
        case MArgument::ArgumentType::VReg:
        case MArgument::ArgumentType::MemImmVReg:
        case MArgument::ArgumentType::MemVReg: {
          auto reg = instr.args[i_arg].reg;
          auto reg_id = reg_to_uid(instr.args[i_arg].reg);
          if (!reg.is_concrete() && !graph.neighbours.contains(reg_id)) {
            set_default_reg_costs(instr.args[i_arg].reg,
                                  graph.node_costs[reg_id]);
            graph.neighbours[reg_id];
          }
        } break;
        case MArgument::ArgumentType::MemVRegVRegScale:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVRegScale: {
          auto reg = instr.args[i_arg].reg;
          auto reg_id = reg_to_uid(instr.args[i_arg].reg);
          auto indx = instr.args[i_arg].indx;
          auto indx_id = reg_to_uid(instr.args[i_arg].indx);
          if (!reg.is_concrete() && !graph.neighbours.contains(reg_id)) {
            set_default_reg_costs(instr.args[i_arg].reg,
                                  graph.node_costs[reg_id]);
            graph.neighbours[reg_id];
          }
          if (!indx.is_concrete() && !graph.neighbours.contains(indx_id)) {
            set_default_reg_costs(instr.args[i_arg].indx,
                                  graph.node_costs[indx_id]);
            graph.neighbours[indx_id];
          }
        } break;
        case MArgument::ArgumentType::MemLabelVregScale:
        case MArgument::ArgumentType::MemLabelVreg:
        case MArgument::ArgumentType::MemImmVRegScale: {
          auto indx = instr.args[i_arg].indx;
          auto indx_id = reg_to_uid(instr.args[i_arg].indx);
          if (!indx.is_concrete() && !graph.neighbours.contains(indx_id)) {
            set_default_reg_costs(instr.args[i_arg].indx,
                                  graph.node_costs[indx_id]);
            graph.neighbours[indx_id];
          }
        } break;
        }
      }

      // discount move like instructions between registers to keep in same
      // regisrer
      if ((instr.is(GBaseSubtype::mov) || instr.is(GBaseSubtype::ret_setup) ||
           instr.is(GBaseSubtype::arg_setup)) &&
          instr.args[0].isReg() && instr.args[1].isReg()) {
        auto r0 = reg_to_uid(instr.args[0].reg);
        auto r1 = reg_to_uid(instr.args[1].reg);
        if (r0 == r1) {
          continue;
        }
        EdgeId edge{r0, r1};
        bool exists = graph.connection_costs.contains(edge);
        auto &addd_costs = graph.connection_costs[edge];
        if (!exists) {
          graph.neighbours[r0].push_back(r1);
          graph.neighbours[r1].push_back(r0);
          addd_costs.hard_constraint = false;
        }
        for (u32 i = 1; i < numRegs; i++) {
          if (addd_costs.cost[i][i] != infCost) {
            addd_costs.cost[i][i] += copyDiscount;
          }
        }
      }
    }
  }
}

void minimize_graph(CostGraph &graph, TVec<StackRecord> &allocation_stack) {
  // fmt::println("Init size {}", graph.neighbours.size());
  while (true) {
    bool found_low_degree_node = false;
    bool found_any_virtual = false;
    // TODO: mazbe should first do all 1degree then all 2degree nodes
    // 1 degree nodes i then oculd also run in parralel?
    auto it = graph.neighbours.begin();
    while (it != graph.neighbours.end()) {
      auto &[node, neigh] = *it;
      if (uid_to_reg(node).is_concrete()) {
        ++it;
        continue;
      }
      found_any_virtual = true;
      if (R1_Reduction(node, neigh, graph, allocation_stack, false) ||
          R2_Reduction(node, neigh, graph, allocation_stack, false)) {
        it = graph.neighbours.erase(it); // Returns the next valid iterator
        found_low_degree_node = true;
      } else {
        ++it; // Only advance if we didn't erase
      }
    }

    // If we processed low-degree nodes, loop again to see if the reductions
    // created *new* low-degree nodes.
    if (found_low_degree_node) {
      continue;
    }
    // if we havent found one we can go ahread and ignore the optional edges
    // that we inserted to improve the register selection which however are not
    // "real" edges. However these "fale" edges still can cause us to spill
    // since for the R1/R2 by default htez look like hard constriants
    while (it != graph.neighbours.end()) {
      auto &[node, neigh] = *it;
      if (uid_to_reg(node).is_concrete()) {
        ++it;
        continue;
      }
      // here we will actually break in hope there has been some fixes that now
      // allow the upper stuff to run
      //  however this might be a bad idea perfomance wise
      found_any_virtual = true;
      if (R1_Reduction(node, neigh, graph, allocation_stack, true)) {
        it = graph.neighbours.erase(it); // Returns the next valid iterator
        found_low_degree_node = true;
        break;
      }
      // if (R2_Reduction(node, neigh, graph, allocation_stack, true)) {
      //   it = graph.neighbours.erase(it); // Returns the next valid iterator
      //   found_low_degree_node = true;
      // }
      ++it;
    }
    if (found_low_degree_node) {
      continue;
    }

    if (graph.neighbours.empty() || !found_any_virtual) {
      break;
    }

    // Spill / Minimum-degree reduction step if no degree 1 or 2 nodes remain
    if (!RM_Reduction(graph, allocation_stack)) {
      break;
    }
  }

  for (auto &[node, neigh] : graph.neighbours) {
    if (uid_to_reg(node).is_concrete()) {
      continue;
    }
    ASSERT(neigh.empty());
    StackRecord record;
    record.node = node;
    record.type = ReductionType::RM;
    record.original_v = TVec<f32>(std::begin(graph.node_costs[node].cost),
                                  std::end(graph.node_costs[node].cost));
    allocation_stack.push_back(record);
  }
}

void actually_allocate(TVec<StackRecord> &allocation_stack,
                       TMap<u64, CReg> &final_assignments,
                       TVec<u64> &needs_spilling) {

  auto get_final_assign = [&final_assignments](u64 neigh) -> CReg {
    if (uid_to_reg(neigh).is_concrete()) {
      return uid_to_reg(neigh).c_reg();
    }
    return final_assignments[neigh];
  };
  while (!allocation_stack.empty()) {
    StackRecord record = allocation_stack.back();
    allocation_stack.pop_back();

    u64 u = record.node;
    f32 selection_vector[numRegs] = {};

    for (u32 i = 0; i < numRegs; i++) {
      selection_vector[i] = record.original_v[i];
    }

    switch (record.type) {
    case ReductionType::R1: {
      auto assigned_reg_neigh0 = get_final_assign(record.neigh0);
      // auto assigned_reg_neigh0 = final_assignments[record.neigh0];

      for (u32 i = 0; i < numRegs; i++) {
        // just go baesd on what the neighbour took
        f32 edge_cost = record.m_ab.get_cost(
            record.ab_flip, i, static_cast<u32>(assigned_reg_neigh0));
        selection_vector[i] += edge_cost;
      }
    } break;
    case ReductionType::R2: {
      auto assigned_reg_neigh0 = get_final_assign(record.neigh0);
      auto assigned_reg_neigh1 = get_final_assign(record.neigh1);
      // auto assigned_reg_neigh0 = final_assignments[record.neigh0];
      // auto assigned_reg_neigh1 = final_assignments[record.neigh1];

      for (u32 i = 0; i < numRegs; i++) {
        // Add interference cost from neighbor 0
        f32 cost_a = record.m_ab.get_cost(
            record.ab_flip, i, static_cast<u32>(assigned_reg_neigh0));
        // Add interference cost from neighbor 1
        f32 cost_b = record.m_bc.get_cost(
            record.bc_flip, i, static_cast<u32>(assigned_reg_neigh1));

        selection_vector[i] += (cost_a + cost_b);
      }
    } break;
    case ReductionType::RM: {
      // evaluate every enighbour edge we broke during the RM phase
      for (const auto &broken : record.broken_edges) {
        auto assigned_reg_neigh = get_final_assign(broken.neighbor);
        // auto assigned_reg_neigh = final_assignments[broken.neighbor];

        for (u32 i = 0; i < numRegs; i++) {
          f32 edge_cost = broken.matrix.get_cost(
              broken.flip, i, static_cast<u32>(assigned_reg_neigh));
          selection_vector[i] += edge_cost;
        }
      }
    } break;
    }

    // chose the best :)
    u32 best_choice = 0;
    // needs to be lower then INF since inf should never be chosen
    f32 min_cost = 1e9F;

    for (u32 i = 0; i < numRegs; i++) {
      if (selection_vector[i] < min_cost) {
        min_cost = selection_vector[i];
        best_choice = i;
      } else if (selection_vector[i] == min_cost && best_choice == 0 &&
                 i != 0) {
        // Prefer physical register over Spill on tie
        best_choice = i;
      }
    }
    auto best_creg = static_cast<CReg>(best_choice);
    if (best_creg == CReg::Virtual) {
      needs_spilling.push_back(u);
    }
    final_assignments[u] = best_creg;
  }
}

bool handle_spill_addr_mode(IRVec<MInstr> &bbm, size_t &instr_id,
                            VReg spill_vreg, u64 stack_slot_id) {
  // try to spill by replacing th register usage in the instruction if posssible
  // with a memory address to the spill register
  auto &instr = bbm[instr_id];
  if (instr.is(GBaseSubtype::mov) || instr.is(GBaseSubtype::arg_setup) ||
      instr.is(GArithSubtype::add2) || instr.is(GArithSubtype::lor2) ||
      instr.is(GArithSubtype::lxor2) || instr.is(GArithSubtype::land2) ||
      instr.is(GArithSubtype::mul2) || instr.is(GArithSubtype::sub2) ||
      instr.is(GConvSubtype::itrunc) || instr.is(GBaseSubtype::ret_setup)) {
    auto &a0 = instr.args[0];
    auto &a1 = instr.args[1];
    // TODO: tehcnically if the argument youre modifying is mem you can in some
    // cases update it
    if (a0.isMem() || a1.isMem()) {
      return false;
    }
    if (a0.uses_same_vreg(spill_vreg)) {
      // imul needs a register destination for first
      if (instr.is(GArithSubtype::mul2)) {
        return false;
      }
      a0 = MArgument::stack_slot(stack_slot_id,
                                 a1.ty == Type::INVALID ? a0.ty : a1.ty);
      return true;
    }
    if (a1.uses_same_vreg(spill_vreg)) {
      a1 = MArgument::stack_slot(stack_slot_id,
                                 a0.ty == Type::INVALID ? a1.ty : a0.ty);
      return true;
    }
  } else if (instr.is(GVecSubtype::vadd) || instr.is(GVecSubtype::vsub)) {
    auto &a2 = instr.args[2];
    if (a2.isMem() || !a2.uses_same_vreg(spill_vreg)) {
      return false;
    }
    a2 = MArgument::stack_slot(stack_slot_id, a2.ty);
    return true;
  } else if (instr.is(GArithSubtype::udiv) || instr.is(GArithSubtype::idiv)) {
    auto &a3 = instr.args[3];
    if (a3.isMem()) {
      return false;
    }
    ASSERT(a3.uses_same_vreg(spill_vreg));
    a3 = MArgument::stack_slot(stack_slot_id, a3.ty);
    return true;
  } else if (instr.is(GConvSubtype::mov_zx) || instr.is(GConvSubtype::mov_sx)
             //||instr.is(X86Subtype::vpcmpeq)
  ) {
    auto &a1 = instr.args[1];
    // TODO: tehcnically if the argument youre modifying is mem you can in some
    // cases update it
    if (a1.isMem() || !a1.uses_same_vreg(spill_vreg)) {
      return false;
    }
    a1 = MArgument::stack_slot(stack_slot_id, a1.ty);
    return true;
  } else {
    fmt::println(">> try mem insert spill >> {}", instr);
  }
  return false;
}

bool handle_spill_scavenger() {
  // MFunc &func, u64 spill_reg_uid, StackSlotId stack_slot
  // scavenge for register and try moving into it
  // TODO
  return false;
}

void handle_spill_move(IRVec<MInstr> &bbm, size_t &instr_id, VReg spill_vreg,
                       u64 stack_slot_id, Type spill_type,
                       u64 &new_virtual_reg_id) {
  // Worst case just move into a new vreg and we restart register allocating
  // is not allowed to fail since its used as backup
  ASSERT(!spill_vreg.is_concrete());
  auto to_insert_prior =
      MInstr(GBaseSubtype::mov, MArgument(spill_vreg, spill_type),
             MArgument::stack_slot(stack_slot_id, spill_type));
  auto to_insert_after = MInstr(
      GBaseSubtype::mov, MArgument::stack_slot(stack_slot_id, spill_type),
      MArgument(spill_vreg, spill_type));
  TVec<ArgData> args;
  auto &instr = bbm[instr_id];

  read_args(instr, args);
  bool read = false;
  bool written = false;
  for (auto &arg : args) {
    if (arg.arg.uses_same_vreg(spill_vreg)) {
      read = true;
    }
  }
  args.clear();
  written_args(instr, args);
  for (auto &arg : args) {
    if (arg.arg.uses_same_vreg(spill_vreg)) {
      if (arg.arg.isMem()) {
        read = true;
      } else {
        written = true;
      }
    }
  }
  ASSERT(read || written);

  // TODO: we cannot insert within a arg_setup call ret_setup chunk so
  // gotta walk backwards/forwards from it to handle it correctly
  new_virtual_reg_id += 1;

  u64 new_vid = new_virtual_reg_id;
  VReg new_vreg = VReg(new_vid, spill_type);
  replace_varg(bbm[instr_id], spill_vreg.virt_id(), new_vreg, true);

  if (read) {
    auto insert_loc = instr_id;
    while (insert_loc > 0 && bbm[insert_loc - 1].is(GBaseSubtype::arg_setup)) {
      insert_loc--;
    }
    bbm.insert(bbm.begin() + static_cast<i64>(insert_loc) + 0,
               MInstr{GBaseSubtype::mov, MArgument{new_vreg, spill_type},
                      MArgument::stack_slot(stack_slot_id, spill_type)});
    instr_id++;
  }
  if (written) {
    bbm.insert(bbm.begin() + static_cast<i64>(instr_id) + 1,
               MInstr{
                   GBaseSubtype::mov,
                   MArgument::stack_slot(stack_slot_id, spill_type),
                   MArgument{new_vreg, spill_type},
               });
    instr_id++;
  }
}

// Figure out the type (and with it the size of the stack slot) of the vregs we
// are about to spill. A vreg can be referenced with different types (for
// example a i64 vreg that is truncated) so we take the biggest one we can find.
TMap<u64, Type> get_spill_types(const MFunc &func,
                                const TVec<u64> &needs_spilling) {
  TMap<u64, Type> types;
  for (auto spill : needs_spilling) {
    types.insert({uid_to_reg(spill).virt_id(), Type::INVALID});
  }
  const auto update = [&types](const VReg &reg) {
    if (reg.is_concrete() || reg.ty == Type::INVALID) {
      return;
    }
    auto it = types.find(reg.virt_id());
    if (it == types.end()) {
      return;
    }
    if (it->second == Type::INVALID ||
        get_size(reg.ty) > get_size(it->second)) {
      it->second = reg.ty;
    }
  };
  for (const auto &bb : func.bbs) {
    for (const auto &instr : bb.instrs) {
      for (size_t arg_id = 0; arg_id < instr.n_args; arg_id++) {
        const auto &arg = instr.args[arg_id];
        if (arg.isReg() || arg.isMem()) {
          update(arg.reg);
        }
        if (arg.isMem()) {
          update(arg.indx);
        }
      }
    }
  }
  for (auto &[id, type] : types) {
    // only happens for registers only used as address and without a type
    if (type == Type::INVALID) {
      type = Type::Int64;
    }
  }
  return types;
}

bool do_spilling(MFunc &func, TVec<u64> &needs_spilling,
                 u64 &new_virtual_reg_id) {
  if (needs_spilling.empty()) {
    return true;
  }
  const auto spill_types = get_spill_types(func, needs_spilling);

  TMap<u64, u64> spill_to_stack_slot;
  for (auto spill : needs_spilling) {
    auto spill_type = spill_types.at(uid_to_reg(spill).virt_id());
    auto stack_slot_id = func.create_stack_slot(get_size(spill_type));
    spill_to_stack_slot.insert({spill, stack_slot_id});
  }
  bool inserted_moves = false;

  for (auto &bb : func.bbs) {
    for (size_t instr_id = 0; instr_id < bb.instrs.size(); instr_id++) {
      for (auto spill : needs_spilling) {
        auto spill_vreg = uid_to_reg(spill);
        if (!bb.instrs[instr_id].uses_vreg(spill_vreg)) {
          continue;
        }
        // the new vreg must have the type of the spilled vreg, otherwise a
        // vec vreg ends up being allocated as a GPR
        auto spill_type = spill_types.at(spill_vreg.virt_id());
        u64 stack_slot_id = spill_to_stack_slot[spill];
        if (handle_spill_addr_mode(bb.instrs, instr_id, spill_vreg,
                                   stack_slot_id)) {
          continue;
        }
        if (handle_spill_scavenger()) {
          continue;
        }
        handle_spill_move(bb.instrs, instr_id, spill_vreg, stack_slot_id,
                          spill_type, new_virtual_reg_id);
        inserted_moves = true;
      }
    }
  }
  // TODO("we can reach this unless we implement it");
  return !inserted_moves;
}

} // namespace
} // namespace foptim::fmir

namespace foptim::fmir {

void RegAlloc2::apply(MFunc &func, const conf::CompConf & /*config*/) {
  ZoneScopedN("reg alloc func");
  TMap<VReg, TSet<size_t>> lifetimes;
  TVec<StackRecord> allocation_stack;
  TMap<u64, CReg> final_assignments;
  TVec<u64> needs_spilling;
  CostGraph graph;
  // fmt::println("+++++++++++++++++++++++++++++");
  // fmt::println("{:cd}", func);

  size_t new_virtual_reg_id = 0;
  // TODO: can be merged whe nwe construct the graph we iterate anyway
  for (auto &mbb : func.bbs) {
    for (auto &instr : mbb.instrs) {
      for (size_t arg_id = 0; arg_id < instr.n_args; arg_id++) {
        auto &arg = instr.args[arg_id];
        switch (arg.type) {
        case MArgument::ArgumentType::StackSlot:
        case MArgument::ArgumentType::Imm:
        case MArgument::ArgumentType::Label:
        case MArgument::ArgumentType::MemLabel:
        case MArgument::ArgumentType::MemImmLabel:
        case MArgument::ArgumentType::MemImm:
          break;
        case MArgument::ArgumentType::VReg:
        case MArgument::ArgumentType::MemImmVReg:
        case MArgument::ArgumentType::MemVReg:
          if (!arg.reg.is_concrete()) {
            new_virtual_reg_id =
                std::max(new_virtual_reg_id, arg.reg.virt_id());
          }
          break;
        case MArgument::ArgumentType::MemVRegVRegScale:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVRegScale:
          if (!arg.reg.is_concrete()) {
            new_virtual_reg_id =
                std::max(new_virtual_reg_id, arg.reg.virt_id());
          }
          if (!arg.indx.is_concrete()) {
            new_virtual_reg_id =
                std::max(new_virtual_reg_id, arg.indx.virt_id());
          }
          break;
        case MArgument::ArgumentType::MemImmVRegScale:
        case MArgument::ArgumentType::MemLabelVreg:
        case MArgument::ArgumentType::MemLabelVregScale:
          if (!arg.indx.is_concrete()) {
            new_virtual_reg_id =
                std::max(new_virtual_reg_id, arg.indx.virt_id());
          }
          break;
        }
      }
    }
  }
  size_t i = 0;
  while (true) {
    graph.clear();
    lifetimes.clear();
    lifetimes = reg_coll(func);
    {
      ZoneScopedN("setup costs");
      setup_costs(func, graph, lifetimes);
    }
    // fmt::println("============");
    // graph.dump();
    if (i > 3) {
      TODO("idk about this");
    }
    i++;

    {
      ZoneScopedN("minimize graph");
      allocation_stack.clear();
      minimize_graph(graph, allocation_stack);
    }

    {
      ZoneScopedN("actually allocate");
      final_assignments.clear();
      needs_spilling.clear();
      actually_allocate(allocation_stack, final_assignments, needs_spilling);
    }

    // maybe todo the ifnal assignments we get out are uids but
    //  replace_vargs expects virutal register ids
    //   but we need uids preior sice we gotta handle also concrete regs in
    //   the allocation scheme which also need to be filtered
    TMap<u64, CReg> final_vreg_assignments;
    for (auto [node, reg] : final_assignments) {
      if (reg == CReg::Virtual || uid_to_reg(node).is_concrete()) {
        continue;
      }
      auto node_reg = uid_to_reg(node);
      ASSERT(!node_reg.is_concrete());
      final_vreg_assignments.insert({node_reg.virt_id(), reg});
    }
    // for (auto [a, b] : final_assignments) {
    //   fmt::println("=== {} -> {}", uid_to_reg(a), VReg(b));
    // }
    // fmt::println("============");
    // for (auto [a, b] : final_vreg_assignments) {
    //   fmt::println("    {} -> {}", VReg(a), VReg(b));
    // }
    // fmt::println("Allocated {} Vars", final_vreg_assignments.size());
    // fmt::println("Spilling {} Vars", needs_spilling.size());

    // if we fail to spill we will insert moves from the stackslot
    // to a helper virtual reg and then retry
    {
      ZoneScopedN("spill and shit");
      if (do_spilling(func, needs_spilling, new_virtual_reg_id)) {
        replace_vargs(func.bbs, final_vreg_assignments);
        break;
      }
    }
  }

  // fmt::println("{:cd}", func);
  // fmt::println("=============================");
  // if (func.name == "_Z11TestOneTypeImEvd") {
  //   fmt::println("{:cd}", func);
  // }
}

} // namespace foptim::fmir
