#include "reg_alloc.hpp"

#include <algorithm>
#include <ankerl/unordered_dense.h>
#include <array>
#include <cstring>
#include <fmt/base.h>
#include <fmt/ranges.h>
#include <limits>
#include <ranges>

#include "mir/analysis/block_info.hpp"
#include "mir/analysis/cfg.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"
#include "mir/optim/calling_conv.hpp"
#include "mir/optim/reg_alloc.hpp"
#include "mir/optim/reg_alloc_solver.hpp"
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
// which registers the calling convention saves for the caller / the callee
const std::array<u8, numRegs> &save_classes() {
  static const auto table = CCallHelper.save_classes();
  return table;
}

// What the cost model needs to know about the function
struct CostModelInput {
  // execution weight of every block
  TVec<f32> block_weights;
  // virtual register uid -> weight of the calls it is alive across
  TMap<size_t, f32> call_crossings;
  // virtual registers with a bigger id were created by spilling
  u64 first_spill_temp_id = 0;
};

void setup_costs(const MFunc &func, CostGraph &graph,
                 const TMap<VReg, TSet<size_t>> &lifetimes,
                 const CostModelInput &model) {
  graph.node_costs.reserve(lifetimes.size());
  graph.neighbours.reserve(lifetimes.size());
  graph.node_uid.reserve(lifetimes.size());
  graph.active.reserve(lifetimes.size());
  graph.active_pos.reserve(lifetimes.size());
  for (const auto &[reg, coll] : lifetimes) {
    auto uid = reg_to_uid(reg);
    u32 node = graph.node(uid);
    graph.set_kind(node, kind_of(reg));

    for (auto c : coll) {
      if (c != uid) {
        // every edge is seen from both endpoints, only register it for the
        // first sighting
        u32 other = graph.node(c);
        if (!graph.has_conn(node, other)) {
          // implicit interference matrix, nothing to store
          graph.add_edge(node, other, CostMatrix{});
        }
      }
    }
  }

  // ensure every vreg is represented even the ones without collisions and
  // make cost reductions, while at it count how often (weighted by how often
  // the block runs) every vreg is used
  TVec<f32> use_weight;
  f32 weight = 1.F;
  const auto ensure_node = [&graph, &use_weight, &weight](const VReg &reg) {
    if (reg.is_concrete()) {
      return;
    }
    auto reg_id = reg_to_uid(reg);
    if (!graph.has_node(reg_id)) {
      u32 node = graph.node(reg_id);
      graph.set_kind(node, kind_of(reg));
    }
    u32 node = graph.node(reg_id);
    if (node >= use_weight.size()) {
      use_weight.resize(graph.n_nodes(), 0.F);
    }
    use_weight[node] += weight;
  };
  for (size_t bb_id = 0; bb_id < func.bbs.size(); bb_id++) {
    weight = model.block_weights[bb_id];
    for (const auto &instr : func.bbs[bb_id].instrs) {
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
        case MArgument::ArgumentType::MemVReg:
          ensure_node(instr.args[i_arg].reg);
          break;
        case MArgument::ArgumentType::MemVRegVRegScale:
        case MArgument::ArgumentType::MemImmVRegVReg:
        case MArgument::ArgumentType::MemVRegVReg:
        case MArgument::ArgumentType::MemImmVRegVRegScale:
          ensure_node(instr.args[i_arg].reg);
          ensure_node(instr.args[i_arg].indx);
          break;
        case MArgument::ArgumentType::MemLabelVregScale:
        case MArgument::ArgumentType::MemLabelVreg:
        case MArgument::ArgumentType::MemImmVRegScale:
          ensure_node(instr.args[i_arg].indx);
          break;
        }
      }

      // discount move like instructions between registers to keep in same
      // regisrer
      if ((instr.is(GBaseSubtype::mov) || instr.is(GBaseSubtype::ret_setup) ||
           instr.is(GBaseSubtype::arg_setup)) &&
          instr.args[0].isReg() && instr.args[1].isReg()) {
        auto r0_uid = reg_to_uid(instr.args[0].reg);
        auto r1_uid = reg_to_uid(instr.args[1].reg);
        if (r0_uid == r1_uid) {
          continue;
        }
        u32 r0 = graph.node(r0_uid);
        u32 r1 = graph.node(r1_uid);
        graph.add_copy_discount(r0, r1);
      }
    }
  }

  use_weight.resize(graph.n_nodes(), 0.F);
  const auto &save_class = save_classes();
  for (u32 node = 0; node < graph.n_nodes(); node++) {
    if (graph.is_concrete(node)) {
      continue;
    }
    const u64 uid = graph.node_uid[node];
    NodeCostInfo info;
    info.use_weight = use_weight[node];
    if (auto it = model.call_crossings.find(uid);
        it != model.call_crossings.end()) {
      info.call_crossings = it->second;
    }
    info.unspillable = uid_to_reg(uid).virt_id() > model.first_spill_temp_id;
    apply_cost_model(graph.node_costs[node], graph.node_kind[node], info,
                     save_class);
  }
}

// the solver only treats `mov d, s` as a preference, so some copies between
// registers that do not interfere (this includes the copy in front of every two
// address instruction) end up with different registers. After coloring try to
// give one side the register of the other, if that is free among all of its
// neighbours. Hottest copies first.
void coalesce_copies(const MFunc &func, const CostModelInput &model,
                     const TMap<VReg, TSet<size_t>> &lifetimes,
                     TMap<u64, CReg> &assignment) {
  struct Copy {
    VReg dst, src;
    f32 weight;
  };
  TVec<Copy> copies;
  for (size_t bb_id = 0; bb_id < func.bbs.size(); bb_id++) {
    for (const auto &instr : func.bbs[bb_id].instrs) {
      if (!instr.is(GBaseSubtype::mov) || instr.n_args != 2 ||
          !instr.args[0].isReg() || !instr.args[1].isReg()) {
        continue;
      }
      const auto d = instr.args[0].reg;
      const auto s = instr.args[1].reg;
      if (d.is_concrete() || s.is_concrete() || d == s ||
          kind_of(d) != kind_of(s) || !assignment.contains(d.virt_id()) ||
          !assignment.contains(s.virt_id())) {
        continue;
      }
      auto it = lifetimes.find(d);
      if (it == lifetimes.end() || it->second.contains(reg_to_uid(s))) {
        continue;
      }
      copies.push_back({d, s, model.block_weights[bb_id]});
    }
  }
  if (copies.empty()) {
    return;
  }
  std::stable_sort(
      copies.begin(), copies.end(),
      [](const Copy &a, const Copy &b) { return a.weight > b.weight; });

  const auto &save_class = save_classes();
  const auto try_move = [&](VReg as_vreg, CReg to) {
    const u64 vid = as_vreg.virt_id();
    const auto from = assignment.at(vid);
    const auto &dom = kKinds[kind_of(as_vreg)];
    if (to == CReg::Virtual || dom.local[static_cast<u8>(to)] == kNoLocal) {
      return false;
    }
    // a value that lives across calls must stay in the same kind of register
    if (save_class[static_cast<u8>(to)] != save_class[static_cast<u8>(from)]) {
      auto it = model.call_crossings.find(reg_to_uid(as_vreg));
      if (it != model.call_crossings.end() && it->second > 0) {
        return false;
      }
    }
    for (auto n : lifetimes.at(as_vreg)) {
      const auto other = uid_to_reg(n);
      if (other.is_concrete()) {
        if (other.c_reg() == to) {
          return false;
        }
      } else if (auto it = assignment.find(other.virt_id());
                 it != assignment.end() && it->second == to) {
        return false;
      }
    }
    assignment[vid] = to;
    return true;
  };

  // moving one copy can enable another one
  for (size_t round = 0; round < 2; round++) {
    bool changed = false;
    for (const auto &c : copies) {
      const auto rd = assignment.at(c.dst.virt_id());
      const auto rs = assignment.at(c.src.virt_id());
      if (rd == rs) {
        continue;
      }
      changed |= try_move(c.dst, rs) || try_move(c.src, rd);
    }
    if (!changed) {
      break;
    }
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
      // the destination of a truncation is narrower than its source, the slot
      // must only be written with the destination size
      const bool own_type = instr.is(GConvSubtype::itrunc) ||
                            a1.ty == Type::INVALID;
      a0 = MArgument::stack_slot(stack_slot_id, own_type ? a0.ty : a1.ty);
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
  } else if (instr.is(GJumpSubtype::cjmp_int_slt) ||
             instr.is(GJumpSubtype::cjmp_int_sge) ||
             instr.is(GJumpSubtype::cjmp_int_sle) ||
             instr.is(GJumpSubtype::cjmp_int_sgt) ||
             instr.is(GJumpSubtype::cjmp_int_ult) ||
             instr.is(GJumpSubtype::cjmp_int_ule) ||
             instr.is(GJumpSubtype::cjmp_int_ugt) ||
             instr.is(GJumpSubtype::cjmp_int_uge) ||
             instr.is(GJumpSubtype::cjmp_int_ne) ||
             instr.is(GJumpSubtype::cjmp_int_eq)) {
    // cmp a0, a1: only one of the two may be memory
    auto &a0 = instr.args[0];
    auto &a1 = instr.args[1];
    if (a0.isMem() || a1.isMem()) {
      return false;
    }
    const bool u0 = a0.uses_same_vreg(spill_vreg);
    const bool u1 = a1.uses_same_vreg(spill_vreg);
    if (u0 == u1) {
      // either not used here or used by both (cmp x, x)
      return false;
    }
    auto &spilled = u0 ? a0 : a1;
    auto &other = u0 ? a1 : a0;
    spilled = MArgument::stack_slot(
        stack_slot_id, other.ty == Type::INVALID ? spilled.ty : other.ty);
    return true;
  } else if (instr.is(GCMovSubtype::cmov_ns) ||
             instr.is(GCMovSubtype::cmov_sgt) ||
             instr.is(GCMovSubtype::cmov_slt) ||
             instr.is(GCMovSubtype::cmov_ult) ||
             instr.is(GCMovSubtype::cmov_sge) ||
             instr.is(GCMovSubtype::cmov_sle) ||
             instr.is(GCMovSubtype::cmov_ne) ||
             instr.is(GCMovSubtype::cmov_eq) ||
             instr.is(GCMovSubtype::cmov_ugt) ||
             instr.is(GCMovSubtype::cmov_uge) ||
             instr.is(GCMovSubtype::cmov_ule)) {
    // cmov_cc(target, val, c1, c2) = cmp c1, c2; cmov target, val
    // target has to stay a register, val may be memory and at most one of
    // c1/c2 may be memory
    auto &target = instr.args[0];
    auto &val = instr.args[1];
    auto &c1 = instr.args[2];
    auto &c2 = instr.args[3];
    if (target.isMem() || val.isMem() || c1.isMem() || c2.isMem() ||
        target.uses_same_vreg(spill_vreg)) {
      return false;
    }
    const bool uv = val.uses_same_vreg(spill_vreg);
    const bool u1 = c1.uses_same_vreg(spill_vreg);
    const bool u2 = c2.uses_same_vreg(spill_vreg);
    if (u1 && u2) {
      return false;
    }
    if (uv) {
      val = MArgument::stack_slot(stack_slot_id, target.ty);
    }
    if (u1) {
      c1 = MArgument::stack_slot(stack_slot_id,
                                 c2.ty == Type::INVALID ? c1.ty : c2.ty);
    }
    if (u2) {
      c2 = MArgument::stack_slot(stack_slot_id,
                                 c1.ty == Type::INVALID ? c2.ty : c1.ty);
    }
    return uv || u1 || u2;
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

// a register that currently holds the value of a spilled vreg within a block,
// later reads in the same block can use it instead of reloading. Temps are
// unspillable so they must not be kept alive for long (register pressure).
struct SpillCache {
  VReg temp;
  size_t step = 0;
};
// how many instructions a reloaded value may be reused for
constexpr size_t MaxReloadShareDistance = 16;

bool writes_vreg(const MInstr &instr, VReg vreg) {
  TVec<ArgData> args;
  written_args(instr, args);
  for (auto &arg : args) {
    if (arg.arg.uses_same_vreg(vreg) && !arg.arg.isMem()) {
      return true;
    }
  }
  return false;
}

void handle_spill_move(IRVec<MInstr> &bbm, size_t &instr_id, VReg spill_vreg,
                       u64 stack_slot_id, Type spill_type,
                       u64 &new_virtual_reg_id, size_t &trailing_inserted,
                       const SpillCache *cached = nullptr,
                       VReg *used_temp = nullptr) {
  // Worst case just move into a new vreg and we restart register allocating
  // is not allowed to fail since its used as backup
  ASSERT(!spill_vreg.is_concrete());
  const auto home = MArgument::stack_slot(stack_slot_id, spill_type);
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
  VReg new_vreg;
  const bool reuse = cached != nullptr && read;
  if (reuse) {
    new_vreg = cached->temp;
  } else {
    new_virtual_reg_id += 1;
    new_vreg = VReg(new_virtual_reg_id, spill_type);
  }
  if (used_temp != nullptr) {
    *used_temp = new_vreg;
  }
  replace_varg(bbm[instr_id], spill_vreg.virt_id(), new_vreg, true);

  if (read && !reuse) {
    auto insert_loc = instr_id;
    while (insert_loc > 0 && bbm[insert_loc - 1].is(GBaseSubtype::arg_setup)) {
      insert_loc--;
    }
    bbm.insert(
        bbm.begin() + static_cast<i64>(insert_loc) + 0,
        MInstr{GBaseSubtype::mov, MArgument{new_vreg, spill_type}, home});
    instr_id++;
  }
  if (written) {
    bbm.insert(bbm.begin() + static_cast<i64>(instr_id) + 1,
               MInstr{
                   GBaseSubtype::mov,
                   home,
                   MArgument{new_vreg, spill_type},
               });
    // instr_id keeps pointing at the original instruction, other spilled vregs
    // of the same instruction still have to be handled
    trailing_inserted++;
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

  TMap<u64, SpillCache> cache;
  for (auto &bb : func.bbs) {
    cache.clear();
    size_t step = 0;
    for (size_t instr_id = 0; instr_id < bb.instrs.size(); instr_id++, step++) {
      if (bb.instrs[instr_id].is(GBaseSubtype::call) ||
          bb.instrs[instr_id].is(GBaseSubtype::invoke)) {
        // a value kept in a temp across a call would need a callee saved reg
        cache.clear();
      }
      // stores inserted behind the instruction by handle_spill_move
      size_t trailing_inserted = 0;
      for (auto spill : needs_spilling) {
        auto spill_vreg = uid_to_reg(spill);
        if (!bb.instrs[instr_id].uses_vreg(spill_vreg)) {
          continue;
        }
        // the new vreg must have the type of the spilled vreg, otherwise a
        // vec vreg ends up being allocated as a GPR
        auto spill_type = spill_types.at(spill_vreg.virt_id());
        u64 stack_slot_id = spill_to_stack_slot[spill];

        const SpillCache *cached = nullptr;
        if (auto it = cache.find(spill); it != cache.end()) {
          if (step - it->second.step <= MaxReloadShareDistance) {
            cached = &it->second;
          } else {
            cache.erase(it);
          }
        }
        // a read that can be served by the cached temp, a memory operand is
        // only worth it if we would have to reload otherwise
        if (cached == nullptr || writes_vreg(bb.instrs[instr_id], spill_vreg)) {
          if (handle_spill_addr_mode(bb.instrs, instr_id, spill_vreg,
                                     stack_slot_id)) {
            // the slot may have been modified, the temp is stale
            cache.erase(spill);
            continue;
          }
        }
        if (handle_spill_scavenger()) {
          continue;
        }
        VReg temp;
        handle_spill_move(bb.instrs, instr_id, spill_vreg, stack_slot_id,
                          spill_type, new_virtual_reg_id, trailing_inserted,
                          cached, &temp);
        // reusing a temp does not extend how long it may be shared
        cache[spill] = SpillCache{
            .temp = temp,
            .step = cached != nullptr && cached->temp == temp ? cached->step
                                                              : step};
        inserted_moves = true;
      }
      instr_id += trailing_inserted;
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
  TVec<CReg> assignments;
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
  // spilling only adds instructions, blocks stay the same
  CostModelInput model;
  model.first_spill_temp_id = new_virtual_reg_id;
  {
    const CFG cfg{func};
    const BlockInfo blocks = analyze_blocks(func, cfg);
    model.block_weights.reserve(func.bbs.size());
    for (size_t b = 0; b < func.bbs.size(); b++) {
      model.block_weights.push_back(
          block_weight(blocks.loop_depth[b], blocks.cold[b] != 0));
    }
  }
  size_t i = 0;
  while (true) {
    graph.clear();
    lifetimes.clear();
    model.call_crossings.clear();
    lifetimes = reg_coll(func, &model.block_weights, &model.call_crossings);
    {
      ZoneScopedN("setup costs");
      setup_costs(func, graph, lifetimes, model);
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
      needs_spilling.clear();
      actually_allocate(graph, allocation_stack, assignments, needs_spilling);
    }

    // the assignments we get out are per graph node but replace_vargs expects
    // virtual register ids. Concrete nodes are not interesting here
    TMap<u64, CReg> final_vreg_assignments;
    final_vreg_assignments.reserve(graph.n_nodes());
    for (u32 node = 0; node < graph.n_nodes(); node++) {
      if (assignments[node] == CReg::Virtual || graph.is_concrete(node)) {
        continue;
      }
      final_vreg_assignments.insert(
          {uid_to_reg(graph.node_uid[node]).virt_id(), assignments[node]});
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
        coalesce_copies(func, model, lifetimes, final_vreg_assignments);
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
