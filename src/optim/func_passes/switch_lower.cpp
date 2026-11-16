#include "optim/func_passes/switch_lower.hpp"

#include <algorithm>

#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "optim/helper/switch_slots.hpp"

namespace foptim::optim {
namespace {
constexpr size_t MinTableCases = 4;
// a cluster of the compare tree that is not a table has at most this many cases
constexpr size_t MaxChainCases = 3;
constexpr i128 MaxDensity = 10;

struct Group {
  // sorted by value
  TVec<SwitchCases::Case> cases;
  std::optional<SlotMap> map;
  [[nodiscard]] bool is_table() const { return map.has_value(); }
};

class Lowering {
 public:
  Lowering(fir::Function &func, SwitchCases &sw, bool bmi2, fir::BasicBlock bb)
      : func(func), ctx(func.ctx), sw(sw), bmi2(bmi2), b(bb), bb(bb) {}

  bool run();

 private:
  fir::Function &func;
  fir::ContextData *ctx;
  SwitchCases &sw;
  bool bmi2;

  fir::Builder b;
  fir::BasicBlock bb;
  fir::ValueR value;
  fir::TypeR value_ty;
  // the edges of the switch before it was destroyed, the default is last
  TVec<fir::BBRefWithArgs> edges;
  TVec<fir::ConstantValueR> case_consts;
  TVec<std::optional<fir::BasicBlock>> tramps;
  // the first edges with a target, edges with the same arguments share a block
  TMap<fir::BasicBlock, TVec<u32>> reps;
  std::optional<fir::BasicBlock> unreach;
  // table clusters need the check, only a single table of a switch with an
  // unreachable default does not
  bool default_reachable = true;

  [[nodiscard]] SlotLimits limits() const {
    SlotLimits l;
    l.max_slots_per_case = MaxDensity;
    l.bmi2 = bmi2;
    return l;
  }
  [[nodiscard]] bool table_ready(std::span<const i128> values) const;
  TVec<Group> cluster(std::span<const SwitchCases::Case> sorted) const;

  fir::BasicBlock edge_target(u32 edge);
  fir::BasicBlock unreachable_bb();
  void emit_group(const Group &g, fir::BasicBlock from);
  void emit_tree(std::span<const Group> groups, fir::BasicBlock from);
  void emit_table(const Group &g, fir::BasicBlock from);
  void emit_chain(const Group &g, fir::BasicBlock from);
};

bool Lowering::table_ready(std::span<const i128> values) const {
  if (default_reachable || value_ty->get_bitwidth() != 64) {
    return false;
  }
  for (size_t i = 0; i < values.size(); i++) {
    if (values[i] != static_cast<i128>(i)) {
      return false;
    }
  }
  return std::ranges::all_of(sw.term->bbs,
                             [](const auto &e) { return e.args.empty(); });
}

// minimal number of clusters, then minimal number of single cases, a cluster is
// a dense range of at least MinTableCases
TVec<Group> Lowering::cluster(std::span<const SwitchCases::Case> sorted) const {
  const size_t n = sorted.size();
  const auto l = limits();
  struct Best {
    u32 clusters = 0;
    u32 singles = 0;
    // start of the last cluster
    u32 from = 0;
  };
  TVec<Best> dp(n + 1);
  for (size_t i = 1; i <= n; i++) {
    dp[i] = {dp[i - 1].clusters + 1, dp[i - 1].singles + 1,
             static_cast<u32>(i - 1)};
    for (size_t j = i - std::min(i, MinTableCases) + 1; j-- > 0;) {
      if (i - j < MinTableCases) {
        continue;
      }
      const i128 range = sorted[i - 1].value - sorted[j].value + 1;
      if (range > l.max_entries) {
        break;
      }
      if (range > MaxDensity * static_cast<i128>(i - j)) {
        continue;
      }
      const Best cand{dp[j].clusters + 1, dp[j].singles, static_cast<u32>(j)};
      if (cand.clusters < dp[i].clusters ||
          (cand.clusters == dp[i].clusters && cand.singles < dp[i].singles)) {
        dp[i] = cand;
      }
    }
  }
  TVec<std::pair<u32, u32>> ranges;
  for (size_t i = n; i > 0; i = dp[i].from) {
    ranges.emplace_back(dp[i].from, static_cast<u32>(i));
  }
  std::ranges::reverse(ranges);

  TVec<Group> groups;
  for (auto [begin, end] : ranges) {
    Group g;
    for (u32 k = begin; k < end; k++) {
      g.cases.push_back(sorted[k]);
    }
    if (end - begin >= MinTableCases) {
      TVec<i128> values;
      for (const auto &c : g.cases) {
        values.push_back(c.value);
      }
      g.map = plan_slots(values, sw.value_bits, l, true, true);
    }
    // single cases join the previous chain
    if (!g.is_table() && !groups.empty() && !groups.back().is_table() &&
        groups.back().cases.size() + g.cases.size() <= MaxChainCases) {
      for (const auto &c : g.cases) {
        groups.back().cases.push_back(c);
      }
      continue;
    }
    groups.push_back(std::move(g));
  }
  return groups;
}

fir::BasicBlock Lowering::unreachable_bb() {
  if (!unreach) {
    unreach = b.append_bb();
    b.at_end(*unreach);
    b.build_unreach();
  }
  return *unreach;
}

// the block a slot switch edge goes to, an edge with arguments gets a block of
// its own that passes them on
fir::BasicBlock Lowering::edge_target(u32 edge) {
  const auto &e = edges[edge];
  if (e.args.empty()) {
    return e.bb;
  }
  u32 rep = edge;
  auto &seen = reps[e.bb];
  for (auto r : seen) {
    if (std::ranges::equal(edges[r].args, e.args)) {
      rep = r;
      break;
    }
  }
  if (rep == edge) {
    seen.push_back(edge);
  }
  if (rep != edge) {
    return edge_target(rep);
  }
  if (!tramps[edge]) {
    auto t = b.append_bb();
    b.at_end(t);
    auto br = b.build_branch(e.bb);
    for (auto arg : e.args) {
      br.add_bb_arg(0, arg);
    }
    tramps[edge] = t;
  }
  return *tramps[edge];
}

void Lowering::emit_table(const Group &g, fir::BasicBlock from) {
  const auto &map = *g.map;
  const bool miss_possible =
      default_reachable || g.cases.size() != sw.cases.size();
  fir::Global keys;
  if (map.needs_keys() && miss_possible) {
    keys = make_slot_global(func,
                            static_cast<size_t>(map.n_slots) * map.key_size(),
                            map.key_size());
    TVec<i128> values;
    for (const auto &c : g.cases) {
      values.push_back(c.value);
    }
    map.fill_keys(values, keys->init_value);
  }
  const u32 default_edge = static_cast<u32>(edges.size() - 1);
  const auto [hit, slot] =
      map.emit(ctx, b, value, from, edges[default_edge], miss_possible, keys);

  const auto hole = edge_target(default_edge);
  TVec<fir::BasicBlock> targets(static_cast<size_t>(map.n_slots), hole);
  for (const auto &c : g.cases) {
    targets[map.slot(c.value)] = edge_target(c.edge);
  }
  TVec<std::pair<fir::ConstantValueR, fir::BasicBlock>> cases;
  for (size_t s = 0; s < targets.size(); s++) {
    cases.emplace_back(ctx->get_constant_int(static_cast<i128>(s), 64),
                       targets[s]);
  }
  const auto dead = unreachable_bb();
  b.at_end(hit);
  b.build_switch(slot, cases, dead);
}

void Lowering::emit_chain(const Group &g, fir::BasicBlock from) {
  TVec<std::pair<fir::ConstantValueR, fir::BasicBlock>> cases;
  for (const auto &c : g.cases) {
    cases.emplace_back(case_consts[c.edge], edges[c.edge].bb);
  }
  b.at_end(from);
  auto def = edges.back();
  auto instr = b.build_switch(value, cases, def.bb);
  for (size_t i = 0; i < g.cases.size(); i++) {
    for (auto arg : edges[g.cases[i].edge].args) {
      instr.add_bb_arg(static_cast<u16>(i), arg);
    }
  }
  for (auto arg : def.args) {
    instr.add_bb_arg(static_cast<u16>(g.cases.size()), arg);
  }
}

void Lowering::emit_group(const Group &g, fir::BasicBlock from) {
  if (g.is_table()) {
    emit_table(g, from);
  } else {
    emit_chain(g, from);
  }
}

void Lowering::emit_tree(std::span<const Group> groups, fir::BasicBlock from) {
  if (groups.size() == 1) {
    emit_group(groups[0], from);
    return;
  }
  const size_t mid = groups.size() / 2;
  const i128 pivot = groups[mid].cases.front().value;
  b.at_end(from);
  auto left = b.append_bb();
  auto right = b.append_bb();
  b.at_end(from);
  auto cond = b.build_int_cmp(
      value, fir::ValueR{ctx->get_constant_int(pivot, sw.value_bits)},
      fir::ICmpInstrSubType::SLT);
  b.build_cond_branch(cond, left, right);
  emit_tree(groups.first(mid), left);
  emit_tree(groups.subspan(mid), right);
}

bool Lowering::run() {
  auto sorted = sw.cases;
  std::ranges::sort(sorted, {}, &SwitchCases::Case::value);
  TVec<i128> values;
  for (const auto &c : sorted) {
    values.push_back(c.value);
  }
  value = sw.value();
  value_ty = value.get_type();
  default_reachable = !sw.default_unreachable();
  if (table_ready(values)) {
    return false;
  }

  // plan everything before the switch is touched
  std::optional<SlotMap> single =
      plan_slots(values, sw.value_bits, limits(), default_reachable, true);
  TVec<Group> groups;
  if (single) {
    groups.push_back({.cases = sorted, .map = std::move(single)});
  } else {
    groups = cluster(sorted);
    if (std::ranges::none_of(groups, &Group::is_table)) {
      return false;
    }
  }

  edges.assign(sw.term->bbs.begin(), sw.term->bbs.end());
  tramps.assign(edges.size(), std::nullopt);
  reps.clear();
  case_consts.clear();
  for (size_t i = 0; i + 1 < edges.size(); i++) {
    case_consts.push_back(sw.term->args[i].as_constant());
  }
  if (!default_reachable) {
    unreach = sw.default_edge().bb;
  }
  sw.term.destroy();

  b.at_end(bb);
  emit_tree(groups, bb);
  return true;
}
}  // namespace

PreservedAnalysis SwitchLower::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedNC("SwitchLower", COLOR_OPTIMF);
  TVec<fir::BasicBlock> switches;
  for (auto bb : func.basic_blocks) {
    if (bb->get_terminator()->is(fir::InstrType::SwitchInstr)) {
      switches.push_back(bb);
    }
  }
  bool modified = false;
  for (auto bb : switches) {
    auto sw = SwitchCases::parse(bb->get_terminator());
    if (!sw || sw->cases.size() < MinTableCases) {
      continue;
    }
    Lowering l{func, *sw, ctx.config->target.features.bmi2, bb};
    modified |= l.run();
  }
  return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
}

}  // namespace foptim::optim
