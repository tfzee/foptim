#include "optim/func_passes/lut_match.hpp"

#include <algorithm>
#include <bit>

#include "ir/basic_block.hpp"
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/cost_model/machine.hpp"
#include "optim/helper/bit_windows.hpp"
#include "optim/helper/inline.hpp"
#include "optim/helper/switch_slots.hpp"
#include "utils/map.hpp"
#include "utils/set.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {
namespace {
using fir::BinaryInstrSubType;
using fir::ICmpInstrSubType;
using fir::InstrType;
using fir::UnaryInstrSubType;

constexpr u32 LeafBits = 8;
constexpr u32 DomainSize = 1U << LeafBits;
constexpr size_t MaxNodes = 64;
// the expression has to be at least this big, a table lookup is a zext, a
// load and for an i1 result nothing else
constexpr size_t MinDeadInstrs = 6;
constexpr u32 MaxEntryBytes = 4;
// eq checks of one value that are worth a switch
constexpr size_t MinSwitchCases = 4;

u64 width_mask(u32 w) { return w >= 64 ? ~u64{0} : ((u64{1} << w) - 1); }

i128 sext(u64 v, u32 w) {
  if (w >= 64) {
    return static_cast<i64>(v);
  }
  const u64 sign = u64{1} << (w - 1);
  return static_cast<i128>(static_cast<i64>((v ^ sign) - sign));
}

struct Operand {
  enum class Kind : u8 { Const, Leaf, Node };
  Kind kind = Kind::Const;
  u64 value = 0;
  u32 node = 0;
};

struct Node {
  fir::Instr instr;
  u32 width = 0;
  u8 n_ops = 0;
  Operand ops[3];
};

struct Val {
  u64 v = 0;
  bool poison = false;
};

class Matcher {
 public:
  TVec<Node> nodes;
  fir::ValueR leaf{};
  bool has_leaf = false;
  TMap<fir::ValueR, u32> index;

  // false if v is not an expression of one 8 bit value
  bool operand(fir::ValueR v, Operand &out) {
    const auto ty = v.get_type();
    if (!ty->is_int() || ty->get_bitwidth() > 64) {
      return false;
    }
    const u32 w = ty->get_bitwidth();
    if (v.is_constant()) {
      if (!v.is_constant_int()) {
        return false;
      }
      out = {
          .kind = Operand::Kind::Const,
          .value = static_cast<u64>(v.as_constant()->as_int()) & width_mask(w)};
      return true;
    }
    if (!v.is_instr() || !supported(v.as_instr())) {
      if (w != LeafBits || (has_leaf && leaf != v)) {
        return false;
      }
      leaf = v;
      has_leaf = true;
      out = {.kind = Operand::Kind::Leaf};
      return true;
    }
    if (const auto it = index.find(v); it != index.end()) {
      out = {.kind = Operand::Kind::Node, .node = it->second};
      return true;
    }
    return add_node(v.as_instr(), out);
  }

  bool add_node(fir::Instr instr, Operand &out) {
    if (nodes.size() >= MaxNodes || !supported(instr) ||
        instr->args.size() > 3) {
      return false;
    }
    Node n;
    n.instr = instr;
    n.width = instr.result_type()->get_bitwidth();
    n.n_ops = static_cast<u8>(instr->args.size());
    ASSERT(n.n_ops <= 3);
    for (u8 i = 0; i < n.n_ops; i++) {
      if (!operand(instr->args[i], n.ops[i])) {
        return false;
      }
    }
    const u32 id = static_cast<u32>(nodes.size());
    nodes.push_back(n);
    index[instr.result()] = id;
    out = {.kind = Operand::Kind::Node, .node = id};
    return true;
  }

 private:
  static bool supported(fir::Instr instr) {
    if (instr->is_multi_result() || !instr.result_type()->is_int()) {
      return false;
    }
    // flags that make an input poison are not modeled
    if (instr->Exact || instr->Disjoint || instr->NNeg || instr->SameSign) {
      return false;
    }
    switch (instr->instr_type) {
      case InstrType::BinaryInstr:
        switch (static_cast<BinaryInstrSubType>(instr->get_instr_subtype())) {
          case BinaryInstrSubType::IntAdd:
          case BinaryInstrSubType::IntSub:
          case BinaryInstrSubType::IntMul:
          case BinaryInstrSubType::Shl:
          case BinaryInstrSubType::Shr:
          case BinaryInstrSubType::AShr:
          case BinaryInstrSubType::And:
          case BinaryInstrSubType::Or:
          case BinaryInstrSubType::Xor:
            return true;
          default:
            return false;
        }
      case InstrType::UnaryInstr:
        return instr->is(UnaryInstrSubType::Not) ||
               instr->is(UnaryInstrSubType::IntNeg);
      case InstrType::ICmp:
      case InstrType::SelectInstr:
      case InstrType::ZExt:
      case InstrType::SExt:
        return true;
      case InstrType::ITrunc:
        return !instr->NSW && !instr->NUW;
      default:
        return false;
    }
  }
};

Val eval_binary(const Node &n, Val a, Val b) {
  const u32 w = n.width;
  const u64 m = width_mask(w);
  const auto sub =
      static_cast<BinaryInstrSubType>(n.instr->get_instr_subtype());
  Val r{.poison = a.poison || b.poison};
  if (r.poison) {
    return r;
  }
  const i128 sa = sext(a.v, w);
  const i128 sb = sext(b.v, w);
  auto check_wrap = [&](i128 exact_signed, u128 exact_unsigned) {
    r.poison = (n.instr->NSW && exact_signed != sext(r.v, w)) ||
               (n.instr->NUW && exact_unsigned != r.v);
  };
  switch (sub) {
    case BinaryInstrSubType::IntAdd:
      r.v = (a.v + b.v) & m;
      check_wrap(sa + sb, static_cast<u128>(a.v) + b.v);
      break;
    case BinaryInstrSubType::IntSub:
      r.v = (a.v - b.v) & m;
      r.poison = (n.instr->NSW && sa - sb != sext(r.v, w)) ||
                 (n.instr->NUW && a.v < b.v);
      break;
    case BinaryInstrSubType::IntMul:
      r.v = (a.v * b.v) & m;
      check_wrap(sa * sb, static_cast<u128>(a.v) * b.v);
      break;
    case BinaryInstrSubType::Shl:
      if (b.v >= w) {
        r.poison = true;
        break;
      }
      r.v = (a.v << b.v) & m;
      r.poison = (n.instr->NUW && (r.v >> b.v) != a.v) ||
                 (n.instr->NSW && (sext(r.v, w) >> b.v) != sa);
      break;
    case BinaryInstrSubType::Shr:
      r.poison = b.v >= w;
      r.v = r.poison ? 0 : a.v >> b.v;
      break;
    case BinaryInstrSubType::AShr:
      r.poison = b.v >= w;
      r.v = r.poison ? 0 : static_cast<u64>(sa >> b.v) & m;
      break;
    case BinaryInstrSubType::And:
      r.v = a.v & b.v;
      break;
    case BinaryInstrSubType::Or:
      r.v = a.v | b.v;
      break;
    case BinaryInstrSubType::Xor:
      r.v = a.v ^ b.v;
      break;
    default:
      ASSERT(false);
  }
  return r;
}

Val eval_icmp(const Node &n, u32 op_width, Val a, Val b) {
  Val r{.poison = a.poison || b.poison};
  if (r.poison) {
    return r;
  }
  const i128 sa = sext(a.v, op_width);
  const i128 sb = sext(b.v, op_width);
  switch (static_cast<ICmpInstrSubType>(n.instr->get_instr_subtype())) {
    case ICmpInstrSubType::EQ:
      r.v = a.v == b.v;
      break;
    case ICmpInstrSubType::NE:
      r.v = a.v != b.v;
      break;
    case ICmpInstrSubType::ULT:
      r.v = a.v < b.v;
      break;
    case ICmpInstrSubType::ULE:
      r.v = a.v <= b.v;
      break;
    case ICmpInstrSubType::UGT:
      r.v = a.v > b.v;
      break;
    case ICmpInstrSubType::UGE:
      r.v = a.v >= b.v;
      break;
    case ICmpInstrSubType::SLT:
      r.v = sa < sb;
      break;
    case ICmpInstrSubType::SLE:
      r.v = sa <= sb;
      break;
    case ICmpInstrSubType::SGT:
      r.v = sa > sb;
      break;
    case ICmpInstrSubType::SGE:
      r.v = sa >= sb;
      break;
    default:
      ASSERT(false);
  }
  return r;
}

u32 operand_width(const Node &n, u32 i) {
  return n.instr->args[i].get_type()->get_bitwidth();
}

// the value of the last node for the input x
Val evaluate(const Matcher &m, u64 x, TVec<Val> &vals) {
  auto get = [&](const Operand &o) -> Val {
    switch (o.kind) {
      case Operand::Kind::Const:
        return {.v = o.value};
      case Operand::Kind::Leaf:
        return {.v = x};
      case Operand::Kind::Node:
        return vals[o.node];
    }
    return {};
  };
  for (size_t i = 0; i < m.nodes.size(); i++) {
    const auto &n = m.nodes[i];
    const u64 mask = width_mask(n.width);
    Val r;
    switch (n.instr->instr_type) {
      case InstrType::BinaryInstr:
        r = eval_binary(n, get(n.ops[0]), get(n.ops[1]));
        break;
      case InstrType::UnaryInstr: {
        const auto a = get(n.ops[0]);
        r = {.v = (n.instr->is(UnaryInstrSubType::Not) ? ~a.v : (~a.v + 1)) &
                  mask,
             .poison = a.poison};
        break;
      }
      case InstrType::ICmp:
        r = eval_icmp(n, operand_width(n, 0), get(n.ops[0]), get(n.ops[1]));
        break;
      case InstrType::SelectInstr: {
        const auto c = get(n.ops[0]);
        r = c.poison ? Val{.poison = true}
                     : get((c.v & 1) != 0 ? n.ops[1] : n.ops[2]);
        break;
      }
      case InstrType::ZExt: {
        r = get(n.ops[0]);
        break;
      }
      case InstrType::SExt: {
        const auto a = get(n.ops[0]);
        r = {.v = static_cast<u64>(sext(a.v, operand_width(n, 0))) & mask,
             .poison = a.poison};
        break;
      }
      case InstrType::ITrunc: {
        const auto a = get(n.ops[0]);
        r = {.v = a.v & mask, .poison = a.poison};
        break;
      }
      default:
        ASSERT(false);
    }
    vals[i] = r;
  }
  return vals.back();
}

// what the cost choices need: the machine and which blocks are in a loop
struct CostEnv {
  cost_model::Machine machine;
  TSet<fir::BasicBlock> loop_blocks;

  [[nodiscard]] bool hot(fir::BasicBlock bb) const {
    return loop_blocks.contains(bb);
  }
};

// instructions that die with the root: all their uses are inside the matched
// expression
size_t dead_instrs(const Matcher &m, fir::Instr root,
                   TSet<fir::Instr> &dead_out) {
  TMap<fir::Instr, u32> inner_uses;
  for (const auto &n : m.nodes) {
    for (u8 i = 0; i < n.n_ops; i++) {
      if (n.ops[i].kind == Operand::Kind::Node) {
        inner_uses[m.nodes[n.ops[i].node].instr] += 1;
      }
    }
  }
  // users of a node come after it, so go from the root down
  size_t dead = 0;
  for (size_t i = m.nodes.size(); i-- > 0;) {
    const auto instr = m.nodes[i].instr;
    const bool is_root = instr == root;
    const bool only_inner =
        !is_root && instr.result().get_n_uses() == inner_uses[instr];
    bool users_dead = only_inner;
    if (only_inner) {
      for (const auto &use : instr->uses) {
        users_dead &= dead_out.contains(use.user) || use.user == root;
      }
    }
    if (is_root || users_dead) {
      dead_out.insert(instr);
      dead++;
    }
  }
  return dead;
}

bool try_root(fir::Context &ctx, fir::Function &func, const CostEnv &env,
              fir::Instr root, TSet<fir::Instr> &covered) {
  const auto root_ty = root.result_type();
  if (root->is_multi_result() || !root_ty->is_int() ||
      root_ty->get_bitwidth() > 64 ||
      root_ty->get_bitwidth() > MaxEntryBytes * 8) {
    return false;
  }
  Matcher m;
  Operand out;
  if (!m.add_node(root, out) || !m.has_leaf) {
    return false;
  }
  TSet<fir::Instr> dead;
  if (dead_instrs(m, root, dead) < MinDeadInstrs) {
    return false;
  }

  const u32 rw = root_ty->get_bitwidth();
  TVec<Val> vals(m.nodes.size());
  TVec<Val> table(DomainSize);
  for (u32 x = 0; x < DomainSize; x++) {
    table[x] = evaluate(m, x, vals);
  }
  // poison entries take the most common value, any value refines poison
  TMap<u64, u32> counts;
  u64 common = 0;
  u32 best = 0;
  for (const auto &e : table) {
    if (!e.poison && ++counts[e.v] > best) {
      best = counts[e.v];
      common = e.v;
    }
  }
  if (best == 0) {
    return false;
  }

  fir::Builder b{root};
  b.before(root);
  fir::ValueR replacement;
  // an i1 with two values whose minority fits in a few windows of 64 inputs
  // can be a range check and a shift of a constant mask per window instead of
  // a load, if the cost model agrees
  std::optional<BitWindows> windows;
  if (rw == 1 && counts.size() == 2) {
    TVec<u64> minority;
    for (u32 x = 0; x < DomainSize; x++) {
      if (!table[x].poison && table[x].v != common) {
        minority.push_back(x);
      }
    }
    windows = plan_bit_windows(minority);
    if (windows && !choose_windows_or_table(
                        *windows, env.hot(root->get_parent()), env.machine)
                        .windows) {
      windows.reset();
    }
  }
  if (counts.size() == 1) {
    replacement = fir::ValueR{ctx->get_constant_int(common, rw)};
  } else if (windows) {
    replacement = emit_bit_windows(ctx.data, b, m.leaf, *windows, common != 0);
  } else {
    const u32 entry = root_ty->get_size();
    auto global = make_slot_global(func, DomainSize * entry, entry);
    for (u32 x = 0; x < DomainSize; x++) {
      const u64 v = table[x].poison ? common : table[x].v;
      for (u32 k = 0; k < entry; k++) {
        global->init_value[(x * entry) + k] = static_cast<u8>(v >> (8 * k));
      }
    }
    auto idx = b.build_zext(m.leaf, ctx->get_int_type(64));
    if (entry > 1) {
      idx = b.build_int_mul(idx, fir::ValueR{ctx->get_constant_int(entry, 64)});
    }
    auto addr =
        b.build_ptr_add(fir::ValueR{ctx->get_constant_value(global)}, idx);
    if (rw == entry * 8) {
      replacement = b.build_load(root_ty, addr, false, false);
    } else {
      auto raw = b.build_load(ctx->get_int_type(entry * 8), addr, false, false);
      replacement = b.build_itrunc(raw, root_ty);
    }
  }
  root.result().replace_all_uses(replacement);
  covered.insert(dead.begin(), dead.end());
  return true;
}
struct EqLeaf {
  fir::ValueR cmp;
  fir::ConstantValueR c;
};

struct OrEqSwitch {
  fir::ValueR value{};
  TVec<EqLeaf> cases{};
  // leaves that stay computed behind the switch
  TVec<fir::ValueR> rest{};
  // the or nodes and eq leaves the switch replaces
  TVec<fir::Instr> nodes{};
};

// An or of equality checks of one value against constants, the other leaves of
// the or are left in rest.
bool collect_or_eq(fir::ValueR condi, OrEqSwitch &out) {
  // eq leaves per compared value, everything else is checked behind the switch
  TMap<fir::ValueR, TVec<EqLeaf>> eqs{};
  TVec<fir::Instr> or_nodes{};
  TVec<fir::ValueR> worklist{condi};
  while (!worklist.empty()) {
    const auto leaf = worklist.back();
    worklist.pop_back();
    // a shared node stays computed for its other users anyway
    if ((leaf != condi && leaf.get_n_uses() != 1) || !leaf.is_instr()) {
      out.rest.push_back(leaf);
      continue;
    }
    const auto leaf_i = leaf.as_instr();
    if (leaf_i->is(fir::BinaryInstrSubType::Or)) {
      or_nodes.push_back(leaf_i);
      worklist.push_back(leaf_i->args[1]);
      worklist.push_back(leaf_i->args[0]);
      continue;
    }
    if (leaf_i->is(fir::ICmpInstrSubType::EQ)) {
      auto lhs = leaf_i->args[0];
      auto rhs = leaf_i->args[1];
      if (lhs.is_constant_int() && !rhs.is_constant()) {
        std::swap(lhs, rhs);
      }
      if (rhs.is_constant_int()) {
        eqs[lhs].push_back({.cmp = leaf, .c = rhs.as_constant()});
        continue;
      }
    }
    out.rest.push_back(leaf);
  }

  // the value with the most distinct constants, the first one on a tie
  for (const auto &[v, leaves] : eqs) {
    const auto ty = v.get_type();
    if (!ty->is_int() || ty->get_bitwidth() < 8 || ty->get_bitwidth() > 64) {
      continue;
    }
    TVec<EqLeaf> distinct{};
    for (const auto &e : leaves) {
      if (std::ranges::none_of(distinct, [&](const EqLeaf &o) {
            return o.c->as_int() == e.c->as_int();
          })) {
        distinct.push_back(e);
      }
    }
    if (distinct.size() > out.cases.size()) {
      out.value = v;
      out.cases = std::move(distinct);
    }
  }
  if (out.cases.size() < MinSwitchCases) {
    return false;
  }
  // the eq checks of the other values stay behind the switch, the duplicates
  // of a case are dropped
  for (const auto &[v, leaves] : eqs) {
    if (v == out.value) {
      for (const auto &e : leaves) {
        out.nodes.push_back(e.cmp.as_instr());
      }
    } else {
      for (const auto &e : leaves) {
        out.rest.push_back(e.cmp);
      }
    }
  }
  out.nodes.insert(out.nodes.end(), or_nodes.begin(), or_nodes.end());
  return true;
}

// The or of the rest leaves, empty when there are none.
fir::ValueR or_of_rest(fir::Builder &b, const TVec<fir::ValueR> &rest) {
  auto cond = rest[0];
  for (size_t i = 1; i < rest.size(); i++) {
    cond = b.build_binary_op(cond, rest[i], fir::BinaryInstrSubType::Or);
  }
  return cond;
}

// An or of equality checks whose constants fit in a few windows of 64 values
// becomes a range check and a shift of a constant mask per window, the other
// leaves are or'd on. Returns the replacement of root, invalid when the windows
// are not planned or the cost model prefers the compares.
fir::ValueR or_eq_into_mask(fir::Context &ctx, const CostEnv &env,
                            fir::Instr root, const OrEqSwitch &s) {
  TVec<u64> values;
  for (const auto &c : s.cases) {
    values.push_back(static_cast<u64>(c.c->as_int()));
  }
  std::ranges::sort(values);
  const auto windows = plan_bit_windows(values);
  if (!windows ||
      !choose_windows_or_compares(*windows, static_cast<u32>(values.size()),
                                  env.hot(root->get_parent()), env.machine)
           .windows) {
    return {};
  }
  fir::Builder b{root};
  b.before(root);
  auto res = emit_bit_windows(ctx.data, b, s.value, *windows, false);
  if (!s.rest.empty()) {
    res = b.build_binary_op(res, or_of_rest(b, s.rest),
                            fir::BinaryInstrSubType::Or);
  }
  return res;
}

// Removes what the switch replaced, the or nodes and the eq leaves that have no
// other user.
void erase_dead(TVec<fir::Instr> nodes) {
  bool progress = true;
  while (progress) {
    progress = false;
    for (auto &n : nodes) {
      if (n.is_valid() && n.result().get_n_uses() == 0) {
        n.destroy();
        n = fir::Instr{};
        progress = true;
      }
    }
  }
}

// A branch on an or of equality checks of one value against constants becomes
// a switch, the other leaves of the or are checked behind it.
bool or_eq_into_switch(fir::Context &ctx, const CostEnv &env,
                       fir::BasicBlock bb) {
  auto term = bb->get_terminator();
  if (!term->is(fir::InstrType::CondBranchInstr)) {
    return false;
  }
  const auto condi = term->args[0];
  if (!condi.is_instr(fir::BinaryInstrSubType::Or)) {
    return false;
  }
  OrEqSwitch s;
  if (!collect_or_eq(condi, s)) {
    return false;
  }

  if (const auto res = or_eq_into_mask(ctx, env, condi.as_instr(), s);
      res.kind != fir::ValueType::InvalidValue) {
    auto cond_mut = condi;
    cond_mut.replace_all_uses(res);
    erase_dead(std::move(s.nodes));
    return true;
  }

  const fir::BBRefWithArgs true_edge = term->bbs[0];
  const fir::BBRefWithArgs false_edge = term->bbs[1];
  term.destroy();

  fir::Builder b{bb};
  fir::BasicBlock default_bb = false_edge.bb;
  if (!s.rest.empty()) {
    default_bb = b.append_bb();
    b.at_end(default_bb);
    auto br =
        b.build_cond_branch(or_of_rest(b, s.rest), true_edge.bb, false_edge.bb);
    for (auto arg : true_edge.args) {
      br.add_bb_arg(0, arg);
    }
    for (auto arg : false_edge.args) {
      br.add_bb_arg(1, arg);
    }
  }

  TVec<std::pair<fir::ConstantValueR, fir::BasicBlock>> targets;
  for (const auto &c : s.cases) {
    targets.emplace_back(c.c, true_edge.bb);
  }
  b.at_end(bb);
  auto sw = b.build_switch(s.value, targets, default_bb);
  for (size_t i = 0; i < targets.size(); i++) {
    for (auto arg : true_edge.args) {
      sw.add_bb_arg(static_cast<u16>(i), arg);
    }
  }
  if (s.rest.empty()) {
    for (auto arg : false_edge.args) {
      sw.add_bb_arg(static_cast<u16>(targets.size()), arg);
    }
  }
  erase_dead(std::move(s.nodes));
  return true;
}

// An i1 or of equality checks used anywhere else: the block is split behind
// the or, the switch decides in front of it and the result is a block argument
// of the block behind (true on every case, the or of the other leaves on the
// default).
bool or_eq_value_into_switch(fir::Context &ctx, const CostEnv &env,
                             fir::Instr root, TSet<fir::Instr> &covered) {
  OrEqSwitch s;
  if (!collect_or_eq(root.result(), s)) {
    return false;
  }
  covered.insert(s.nodes.begin(), s.nodes.end());
  if (const auto res = or_eq_into_mask(ctx, env, root, s);
      res.kind != fir::ValueType::InvalidValue) {
    root.result().replace_all_uses(res);
    erase_dead(std::move(s.nodes));
    return true;
  }

  const auto bb = root->get_parent();
  auto end_bb = split_block(root);
  end_bb.add_arg(ctx->storage.insert_bb_arg(end_bb, root.result_type()));
  root.result().replace_all_uses(fir::ValueR{end_bb->args[0]});

  const fir::ValueR yes{ctx->get_constant_int(1, 1)};
  const fir::ValueR no{ctx->get_constant_int(0, 1)};
  fir::Builder b{bb};
  fir::BasicBlock default_bb = end_bb;
  if (!s.rest.empty()) {
    default_bb = b.append_bb();
    b.at_end(default_bb);
    const auto cond = or_of_rest(b, s.rest);
    b.build_branch(end_bb).add_bb_arg(0, cond);
  }
  TVec<std::pair<fir::ConstantValueR, fir::BasicBlock>> targets;
  for (const auto &c : s.cases) {
    targets.emplace_back(c.c, end_bb);
  }
  b.at_end(bb);
  auto sw = b.build_switch(s.value, targets, default_bb);
  for (size_t i = 0; i < targets.size(); i++) {
    sw.add_bb_arg(static_cast<u16>(i), yes);
  }
  if (s.rest.empty()) {
    sw.add_bb_arg(static_cast<u16>(targets.size()), no);
  }
  erase_dead(std::move(s.nodes));
  return true;
}
}  // namespace

PreservedAnalysis LUTMatch::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedNC("LUTMatch", COLOR_OPTIMF);
  CostEnv env{.machine = cost_model::Machine::from_target(ctx.config->target),
              .loop_blocks = {}};
  if (func.basic_blocks.size() > 1) {
    CFG &cfg = AnalysisManager::cfg(func);
    Dominators &dom = AnalysisManager::dom(func);
    const LoopInfoAnalysis loops{dom};
    for (const auto &loop : loops.info) {
      for (const u32 id : loop.body_nodes) {
        env.loop_blocks.insert(cfg.bbrs[id].bb);
      }
    }
  }
  TVec<fir::Instr> candidates;
  for (auto bb : func.basic_blocks) {
    for (auto instr : bb->get_instrs()) {
      candidates.push_back(instr);
    }
  }
  bool changed = false;
  TSet<fir::Instr> covered;
  // users come after their operands, so the biggest expression is tried first
  for (size_t i = candidates.size(); i-- > 0;) {
    const auto instr = candidates[i];
    if (covered.contains(instr) || instr->n_results() != 1 ||
        instr.result().get_n_uses() == 0) {
      continue;
    }
    changed |= try_root(ctx, func, env, instr, covered);
  }
  // what no table was made of: a branch on eq checks of one value (a value
  // too wide to evaluate) becomes a switch
  TVec<fir::BasicBlock> branches;
  for (auto bb : func.basic_blocks) {
    const auto term = bb->get_terminator();
    if (term->is(InstrType::CondBranchInstr) &&
        term->args[0].is_instr(BinaryInstrSubType::Or)) {
      branches.push_back(bb);
    }
  }
  bool cfg_changed = false;
  for (auto bb : branches) {
    cfg_changed |= or_eq_into_switch(ctx, env, bb);
  }
  // the same for an or that is returned or passed on
  TVec<fir::Instr> ors;
  for (auto bb : func.basic_blocks) {
    for (auto instr : bb->get_instrs()) {
      if (instr->is(BinaryInstrSubType::Or) && instr->n_results() == 1 &&
          instr.result().get_n_uses() != 0 && instr.result_type()->is_int() &&
          instr.result_type()->get_bitwidth() == 1) {
        ors.push_back(instr);
      }
    }
  }
  for (size_t i = ors.size(); i-- > 0;) {
    if (!covered.contains(ors[i])) {
      cfg_changed |= or_eq_value_into_switch(ctx, env, ors[i], covered);
    }
  }
  if (cfg_changed) {
    return PreservedAnalysis::none();
  }
  return changed ? PreservedAnalysis::cfg_only() : PreservedAnalysis::all();
}

}  // namespace foptim::optim
