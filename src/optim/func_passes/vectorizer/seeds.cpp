#include "optim/func_passes/vectorizer/seeds.hpp"

#include <algorithm>

#include "optim/func_passes/vectorizer/bundle.hpp"

namespace foptim::optim::vec {

namespace {
bool is_chain_op(fir::ValueR v, u32 sub_type) {
  return lane_instr(v) &&
         v.as_instr()->instr_type == fir::InstrType::BinaryInstr &&
         v.as_instr()->subtype == sub_type;
}

// Collects the inputs of a chain that only nests on one side, add(add(x, y), z)
// for the left and add(z, add(y, x)) for the right. Not associative operations
// need the lanes in this order.
void red_search(TVec<fir::ValueR> &reduction_inputs, fir::ValueR curr_base,
                u32 exp_sub_type, bool left) {
  if (!lane_instr(curr_base)) {
    reduction_inputs.clear();
    return;
  }
  auto curr_i = curr_base.as_instr();
  if (!is_chain_op(curr_base, exp_sub_type)) {
    reduction_inputs.push_back(curr_base);
    return;
  }
  const auto inner = curr_i->args[left ? 0 : 1];
  const auto outer = curr_i->args[left ? 1 : 0];
  if (is_chain_op(inner, exp_sub_type) && lane_instr(outer)) {
    red_search(reduction_inputs, inner, exp_sub_type, left);
    reduction_inputs.push_back(outer);
  } else if (lane_instr(inner) && lane_instr(outer)) {
    reduction_inputs.push_back(inner);
    reduction_inputs.push_back(outer);
  } else {
    reduction_inputs.push_back(curr_base);
  }
}

// The lanes may be taken in any order, the inner nodes of the chain must have
// no other user.
// TODO: pick the order so that loads end up adjacent
bool red_search_assoc(TVec<fir::ValueR> &reduction_inputs,
                      fir::ValueR curr_base, fir::InstrType exp_type,
                      u32 exp_sub_type, fir::BasicBlock bb, bool is_root) {
  if (!lane_instr(curr_base)) {
    return false;
  }
  auto curr_i = curr_base.as_instr();
  bool inner = !is_root && curr_i->instr_type == exp_type &&
               curr_i->subtype == exp_sub_type && curr_i->parent == bb &&
               curr_base.get_n_uses() == 1;
  if (!inner && !is_root) {
    reduction_inputs.push_back(curr_base);
    return true;
  }
  return red_search_assoc(reduction_inputs, curr_i->args[0], exp_type,
                          exp_sub_type, bb, false) &&
         red_search_assoc(reduction_inputs, curr_i->args[1], exp_type,
                          exp_sub_type, bb, false);
}
}  // namespace

std::optional<Seed> find_reduction(fir::BasicBlock bb, size_t instr_id,
                                   const conf::CompConf &conf) {
  Seed curr;
  auto base_instr = bb->instructions[instr_id];
  auto subtype = base_instr->subtype;

  bool isProd = false;
  bool isSum = false;
  bool isFold = false;
  bool is_associative = false;
  if (base_instr->is(fir::InstrType::BinaryInstr)) {
    auto binsub = static_cast<fir::BinaryInstrSubType>(subtype);
    // the backend has horizontal add and fp mul, the rest is folded lane by
    // lane
    isProd = binsub == fir::BinaryInstrSubType::FloatMul;
    isSum = binsub == fir::BinaryInstrSubType::FloatAdd ||
            binsub == fir::BinaryInstrSubType::IntAdd;
    isFold = binsub == fir::BinaryInstrSubType::IntMul ||
             binsub == fir::BinaryInstrSubType::And ||
             binsub == fir::BinaryInstrSubType::Or ||
             binsub == fir::BinaryInstrSubType::Xor;
    is_associative = binsub == fir::BinaryInstrSubType::IntAdd || isFold;
    is_associative |= conf.optim.fltOpt.associative_math &&
                      binsub == fir::BinaryInstrSubType::FloatAdd;
  } else if (base_instr->is(fir::InstrType::Intrinsic)) {
    switch (static_cast<fir::IntrinsicSubType>(subtype)) {
      case fir::IntrinsicSubType::SMin:
      case fir::IntrinsicSubType::SMax:
      case fir::IntrinsicSubType::UMin:
      case fir::IntrinsicSubType::UMax:
        isFold = true;
        break;
      case fir::IntrinsicSubType::FMin:
      case fir::IntrinsicSubType::FMax:
        isFold = conf.optim.fltOpt.associative_math;
        break;
      default:
        break;
    }
    is_associative = isFold;
  }
  if (!isProd && !isSum && !isFold) {
    return {};
  }

  // TODO: find bigger ones
  if (!lane_instr(base_instr->args[0]) || !lane_instr(base_instr->args[1])) {
    return {};
  }
  TVec<fir::ValueR> res1;
  TVec<fir::ValueR> res2;
  if (is_associative) {
    if (!red_search_assoc(res1, base_instr.result(), base_instr->instr_type,
                          subtype, bb, true)) {
      res1.clear();
    }
  } else {
    red_search(res1, base_instr.result(), subtype, true);
    red_search(res2, base_instr.result(), subtype, false);
  }
  const auto &inputs = res1.size() >= res2.size() ? res1 : res2;
  const auto max_size = inputs.size();
  const bool size_ok =
      max_size == 2 || max_size == 4 ||
      (max_size == 8 && base_instr.result_type()->get_bitwidth() == 4);
  if (!size_ok) {
    return {};
  }
  TVec<SeedInstrData> out;
  for (auto r : inputs) {
    out.push_back(SeedInstrData{.instr = r.as_instr()});
  }
  return Seed{.base = base_instr.result(),
              .type = base_instr.result_type(),
              .data = out,
              .assoc = is_associative};
}

namespace {

void sort_by_size(TVec<Seed> &seeds) {
  std::ranges::sort(seeds, [](const Seed &b1, const Seed &b2) {
    return b1.data.size() > b2.data.size();
  });
}

// Loads from one base with constant offsets are put in address order, so
// adjacent loads end up in adjacent lanes.
void sort_adjacent_loads(TVec<SeedInstrData> &data) {
  PtrShape first;
  TVec<std::pair<i64, SeedInstrData>> keyed;
  for (auto &d : data) {
    if (!d.instr->is(fir::InstrType::LoadInstr)) {
      return;
    }
    auto shape = decompose_ptr(d.instr->args[0]);
    if (!keyed.empty() && !shape.same_base(first)) {
      return;
    }
    first = shape;
    keyed.emplace_back(shape.offset, d);
  }
  std::ranges::stable_sort(
      keyed, [](const auto &x, const auto &y) { return x.first < y.first; });
  for (size_t i = 0; i < keyed.size(); i++) {
    data[i] = keyed[i].second;
  }
}

void finalize_reductions(TVec<Seed> &reduction_seeds) {
  sort_by_size(reduction_seeds);
  // find_reduction only produces 2, 4 and 8 inputs
  std::erase_if(reduction_seeds, [](const Seed &s) {
    const auto size = s.type->get_size();
    // TODO: 8 and 16 bit elements are disabled for now
    return size < 4 || (size == 4 && s.data.size() < 4);
  });
  for (auto &s : reduction_seeds) {
    if (s.assoc) {
      sort_adjacent_loads(s.data);
    }
  }
}

// Stores to adjacent memory become seeds. Candidates are stores at a constant
// offset from a base and variable part. They are grouped by both, sorted by
// barrier epoch and offset and cut into runs of adjacent stores of one type. A
// run is cut into windows of a width the target supports, widest first, and
// each window has to pass can_sink. Windows never overlap.
class StoreSeedProvider final : public SeedProvider {
  struct Cand {
    u32 access;
    PtrShape shape;
    SeedInstrData data;
    fir::TypeR type;
    u32 epoch;
  };

 public:
  void scan(SeedRegion &r) final {
    if (r.info == nullptr) {
      return;
    }
    const auto &features = r.conf.target.features;
    const auto &mem = r.info->mem();
    const auto &acc = mem.accesses();

    TVec<Cand> cands;
    for (u32 i = 0; i < acc.size(); i++) {
      if (acc[i].kind != BlockMemAccess::Kind::Store) {
        continue;
      }
      auto instr = acc[i].instr;
      auto ty = instr.result_type();
      if (!ty->is_int() && !ty->is_float() && !ty->is_ptr()) {
        continue;
      }
      if (ty->get_size() != 4 && ty->get_size() != 8) {
        continue;
      }
      cands.push_back(Cand{.access = i,
                           .shape = acc[i].loc.ptr,
                           .data = SeedInstrData{.instr = instr},
                           .type = ty,
                           .epoch = acc[i].epoch});
    }

    // stores with the same base and variable part, offsets are comparable
    TMap<fir::ValueR, TVec<TVec<u32>>> groups;
    for (u32 i = 0; i < cands.size(); i++) {
      auto &subs = groups[cands[i].shape.base];
      TVec<u32> *found = nullptr;
      for (auto &sub : subs) {
        if (cands[sub[0]].shape.same_terms(cands[i].shape)) {
          found = &sub;
          break;
        }
      }
      if (found == nullptr) {
        subs.emplace_back();
        found = &subs.back();
      }
      found->push_back(i);
    }
    u32 max_bits = features.avx512f ? 512 : 256;
    for (auto &[base, subs] : groups) {
      for (auto &group : subs) {
        std::ranges::sort(group, [&](u32 a, u32 b) {
          const auto &ca = cands[a];
          const auto &cb = cands[b];
          if (ca.epoch != cb.epoch) {
            return ca.epoch < cb.epoch;
          }
          if (ca.shape.offset != cb.shape.offset) {
            return ca.shape.offset < cb.shape.offset;
          }
          return ca.access < cb.access;
        });
        size_t run_start = 0;
        for (size_t i = 1; i <= group.size(); i++) {
          bool extends = false;
          if (i < group.size()) {
            const auto &prev = cands[group[i - 1]];
            const auto &cur = cands[group[i]];
            extends =
                cur.epoch == prev.epoch && cur.type == prev.type &&
                cur.shape.offset ==
                    prev.shape.offset + static_cast<i64>(prev.type->get_size());
          }
          if (extends) {
            continue;
          }
          if (i - run_start >= 2) {
            cut_run(r, cands,
                    std::span{group}.subspan(run_start, i - run_start),
                    max_bits);
          }
          run_start = i;
        }
      }
    }
  }

  bool plan(PlanEnv &env, Seed &seed, Plan &plan) final {
    return plan_bundle(env.ctx, env.ctx.config->target.features, seed, false,
                       plan);
  }

  void finalize(const conf::CompConf & /*conf*/) final {
    std::ranges::stable_sort(seeds, [](const Seed &b1, const Seed &b2) {
      return b1.data.size() > b2.data.size();
    });
  }

 private:
  // Windows are cut widest first. The halves of a window are seeds too, they
  // overlap the window, so they are only used when the window could not be
  // planned (a ClaimSet keeps the seeds apart).
  void cut_run(SeedRegion &r, const TVec<Cand> &cands, std::span<const u32> run,
               u32 max_bits) {
    size_t i = 0;
    while (i < run.size()) {
      bool cut = false;
      for (u32 w = 32; w >= 2 && !cut; w /= 2) {
        if (!try_window(r, cands, run, i, w, max_bits)) {
          continue;
        }
        for (u32 sw = w / 2; sw >= 2; sw /= 2) {
          for (size_t k = i; k + sw <= i + w; k += sw) {
            try_window(r, cands, run, k, sw, max_bits);
          }
        }
        i += w;
        cut = true;
      }
      if (!cut) {
        i++;
      }
    }
  }

  bool try_window(SeedRegion &r, const TVec<Cand> &cands,
                  std::span<const u32> run, size_t i, u32 w, u32 max_bits) {
    const u32 bits = cands[run[0]].type->get_size() * 8;
    if (i + w > run.size() || w * bits < 128 || w * bits > max_bits) {
      return false;
    }
    TVec<u32> members;
    for (size_t j = 0; j < w; j++) {
      members.push_back(cands[run[i + j]].access);
    }
    if (r.info->mem().can_sink(members, r.aa) != MemDep::Independent) {
      return false;
    }
    Seed bundle;
    bundle.base = cands[run[i]].shape.base;
    bundle.type = cands[run[i]].type;
    bundle.block = r.info;
    bundle.kind = SeedKind::Store;
    for (size_t j = 0; j < w; j++) {
      bundle.data.push_back(cands[run[i + j]].data);
    }
    seeds.push_back(std::move(bundle));
    return true;
  }
};

class ReductionSeedProvider final : public SeedProvider {
 public:
  void scan(SeedRegion &r) final {
    if (r.info == nullptr || !r.config.reductions) {
      return;
    }
    for (size_t i = 0; i < r.info->bb->instructions.size(); i++) {
      if (auto res = find_reduction(r.info->bb, i, r.conf)) {
        res->block = r.info;
        res->kind = SeedKind::Reduction;
        seeds.push_back(res.value());
      }
    }
  }
  bool plan(PlanEnv &env, Seed &seed, Plan &plan) final {
    return plan_bundle(env.ctx, env.ctx.config->target.features, seed, true,
                       plan);
  }
  void finalize(const conf::CompConf & /*conf*/) final {
    finalize_reductions(seeds);
  }
};
}  // namespace
std::unique_ptr<SeedProvider> make_store_seeds() {
  return std::make_unique<StoreSeedProvider>();
}
std::unique_ptr<SeedProvider> make_reduction_seeds() {
  return std::make_unique<ReductionSeedProvider>();
}

}  // namespace foptim::optim::vec
