#include "optim/func_passes/vectorizer/vectorizer.hpp"

#include <algorithm>

#include "optim/analysis/dominators.hpp"
#include "optim/func_passes/vectorizer/bundle.hpp"
#include "optim/func_passes/vectorizer/loop.hpp"
#include "optim/func_passes/vectorizer/region.hpp"
#include "optim/func_passes/vectorizer/seeds.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {

using namespace vec;

namespace {
// the straight line providers of one block, the seeds point into them
struct BlockSeeds {
  std::unique_ptr<SeedProvider> stores = make_store_seeds();
  std::unique_ptr<SeedProvider> reductions = make_reduction_seeds();
};

void find_seeds(const conf::CompConf &conf, const Config &config,
                BlockInfo &info, AliasAnalyis &aa, BlockSeeds &out) {
  SeedProvider *providers[] = {out.stores.get(), out.reductions.get()};
  const char *seen_names[] = {"SLPSeedsStore", "SLPSeedsReduction"};
  const char *rejected_names[] = {"SLPSeedsRejectedStore",
                                  "SLPSeedsRejectedReduction"};
  SeedRegion region{
      .info = &info, .loop = nullptr, .aa = aa, .conf = conf, .config = config};
  auto &stats = utils::StatCollector::get();
  size_t idx = 0;
  for (auto *p : providers) {
    p->scan(region);
    auto found = p->seeds.size();
    p->finalize(conf);
    stats.addi(static_cast<i64>(found), seen_names[idx],
               utils::StatCollector::StatFOptim);
    stats.addi(static_cast<i64>(found - std::min(found, p->seeds.size())),
               rejected_names[idx], utils::StatCollector::StatFOptim);
    idx++;
  }
}

// instructions that a plan took, no other plan may use them as a seed
class ClaimSet {
 public:
  [[nodiscard]] bool overlaps(const Seed &s) const {
    for (const auto &d : s.data) {
      if (!d.instr.is_valid() || claimed.contains(d.instr.result())) {
        return true;
      }
    }
    return false;
  }
  void claim(const Seed &s) {
    for (const auto &d : s.data) {
      claimed.insert(d.instr.result());
    }
  }

 private:
  // grows inside the temp arena windows of the bundles, so not a TSet
  FDSet<fir::ValueR> claimed;
};
}  // namespace

i64 Vectorizer::slp_benefit(fir::Context &ctx, AliasAnalyis &aa,
                            fir::BasicBlock bb) {
  const auto &features = ctx.config->target.features;
  BlockInfo info{bb};
  BlockSeeds seeds;
  find_seeds(*ctx.config, config, info, aa, seeds);
  // these live in the temp arena and are used by the plans, so they must not
  // be (re)built inside the windows below
  info.order.ensure();
  info.mem();
  ClaimSet claims;
  i64 total = 0;
  for (auto [provider, reduction] : {std::pair{seeds.stores.get(), false},
                                     std::pair{seeds.reductions.get(), true}}) {
    for (auto &s : provider->seeds) {
      if (claims.overlaps(s)) {
        continue;
      }
      auto window = utils::TempAlloc<void *>::save();
      Plan plan;
      if (plan_bundle(ctx, features, s, reduction, plan) && plan.benefit > 0) {
        total += plan.benefit;
        claims.claim(s);
      }
      utils::TempAlloc<void *>::restore(window);
    }
  }
  return total;
}

bool Vectorizer::vectorize_loops(fir::Context &ctx, fir::Function &func,
                                 AliasAnalyis &aa, Claimed &claimed) {
  ZoneScopedN("LoopVectorize");
  CFG &cfg = AnalysisManager::cfg(func);
  Dominators &dom = AnalysisManager::dom(func);
  LoopInfoAnalysis linfo{dom};
  bool changed = false;

  // The vector loop (vector types) and the remainder (two entries) are never
  // candidates again. Applying a plan changes the CFG, so one loop per round.
  for (u32 round = 0; round < 64; round++) {
    for (auto &loop : linfo.info) {
      changed |= route_exit_uses(ctx, cfg, loop);
    }
    auto provider = make_loop_seeds();
    for (auto &loop : linfo.info) {
      SeedRegion region{.info = nullptr,
                        .loop = &loop,
                        .aa = aa,
                        .conf = *ctx.config,
                        .config = config};
      provider->scan(region);
    }
    provider->finalize(*ctx.config);
    PlanEnv env{.ctx = ctx,
                .func = func,
                .cfg = &cfg,
                .dom = &dom,
                .aa = aa,
                .config = config};
    RegionTree tree{cfg, linfo.info};
    TVec<Plan> plans;
    plans.reserve(provider->seeds.size());
    auto &stats = utils::StatCollector::get();
    stats.addi(static_cast<i64>(provider->seeds.size()), "LoopSeeds",
               utils::StatCollector::StatFOptim);
    for (auto &seed : provider->seeds) {
      Plan plan;
      if (!provider->plan(env, seed, plan)) {
        stats.addi(1, "LoopRejectedNoPlan", utils::StatCollector::StatFOptim);
        continue;
      }
      plans.push_back(std::move(plan));
      tree.regions[static_cast<u32>(seed.loop - linfo.info.data())].plan =
          &plans.back();
    }
    if (plans.empty()) {
      break;
    }
    const auto chosen = select_plans(
        tree, config.assumed_trip,
        [&](fir::BasicBlock bb) { return slp_benefit(ctx, aa, bb); });
    stats.addi(
        static_cast<i64>(plans.size() - std::min(plans.size(), chosen.size())),
        "LoopRejectedSelector", utils::StatCollector::StatFOptim);
    if (chosen.empty()) {
      break;
    }
    auto &plan = *tree.regions[chosen[0]].plan;
    const auto header = plan.loop.header;
    const auto blocks = emit_loop(ctx, plan);
    claimed.push_back(header);
    claimed.push_back(blocks.pre);
    claimed.push_back(blocks.vector);
    claimed.push_back(blocks.middle);
    utils::StatCollector::get().addi(1, "loopVectorized",
                                     utils::StatCollector::StatFOptim);
    changed = true;
    cfg.update(func, false);
    dom.update(cfg);
    linfo.update(dom);
  }
  return changed;
}

PreservedAnalysis Vectorizer::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedN("Vectorizer");
  AliasAnalyis aa{};
  Claimed claimed;
  const bool loops_changed =
      config.loops && vectorize_loops(ctx, func, aa, claimed);

  // one entry per block, must not move once seeds point into it
  TVec<BlockInfo> blocks;
  for (auto bb : func.basic_blocks) {
    if (std::ranges::find(claimed, bb) == claimed.end()) {
      blocks.emplace_back(bb);
    }
  }
  // seeds of all blocks, the stores first
  std::vector<BlockSeeds> block_seeds(blocks.size());
  {
    ZoneScopedN("Find Seeds");
    size_t n_seeds = 0;
    for (size_t i = 0; i < blocks.size(); i++) {
      find_seeds(*ctx.config, config, blocks[i], aa, block_seeds[i]);
      n_seeds += block_seeds[i].stores->seeds.size() +
                 block_seeds[i].reductions->seeds.size();
    }
    if (n_seeds == 0) {
      return loops_changed ? PreservedAnalysis::none()
                           : PreservedAnalysis::all();
    }
  }

  auto &stats = utils::StatCollector::get();
  bool modified = false;
  ClaimSet claims;
  PlanEnv env{.ctx = ctx,
              .func = func,
              .cfg = nullptr,
              .dom = nullptr,
              .aa = aa,
              .config = config};
  auto run = [&](bool reduction) {
    for (auto &bs : block_seeds) {
      auto *provider = reduction ? bs.reductions.get() : bs.stores.get();
      for (auto &s : provider->seeds) {
        if (claims.overlaps(s)) {
          continue;
        }
        // these live in the temp arena and are used by the plan, so they must
        // not be (re)built inside the window below
        s.block->order.ensure();
        s.block->mem();
        auto window = utils::TempAlloc<void *>::save();
        Plan plan;
        if (!provider->plan(env, s, plan)) {
          stats.addi(1, "SLPRejectedNoPlan", utils::StatCollector::StatFOptim);
          continue;
        }
        if (plan.nodes.empty() || plan.benefit <= 0) {
          stats.addi(1, "SLPRejectedCost", utils::StatCollector::StatFOptim);
          utils::TempAlloc<void *>::restore(window);
          continue;
        }
        claims.claim(s);
        apply_bundle(ctx, plan, s, reduction);
        stats.addi(1, "SLPVectorized", utils::StatCollector::StatFOptim);
        modified = true;
        s.block->invalidate();
        utils::TempAlloc<void *>::restore(window);
      }
    }
  };
  {
    ZoneScopedN("Run");
    run(false);
    run(true);
  }

  if (loops_changed) {
    return PreservedAnalysis::none();
  }
  // the straight line part only changes instructions, never blocks or edges
  return modified ? PreservedAnalysis::cfg_only() : PreservedAnalysis::all();
}

}  // namespace foptim::optim
