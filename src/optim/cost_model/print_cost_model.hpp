#pragma once
#include <fmt/base.h>

#include "config/compiler_config.hpp"
#include "ir/function.hpp"
#include "ir/print_ids.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include "optim/cost_model/aggregators.hpp"
#include "optim/cost_model/cost.hpp"
#include "optim/cost_model/fir_lowering_estimate.hpp"
#include "optim/cost_model/machine.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {
// debug pass dumping the cost model predictions for golden tests, like
// PrintAnalysis. show option picks the parts, e.g.
// PrintCostModel{show=["instrs", "blocks", "loops"]}
class PrintCostModel final : public FunctionPass {
 public:
  enum Show : u8 {
    Instrs = 1U << 0,
    Blocks = 1U << 1,
    Loops = 1U << 2,
  };
  struct Config {
    u8 show = 0;
  };
  Config config;

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    const auto m = cost_model::Machine::from_target(ctx.config->target);
    fir::PrintIdsScope id_scope;

    if ((config.show & Instrs) != 0) {
      print_instrs(func, m);
    }
    if ((config.show & Blocks) != 0) {
      print_blocks(func, m);
    }
    if ((config.show & Loops) != 0) {
      print_loops(func, m);
    }
    return PreservedAnalysis::all();
  }

 private:
  static std::string short_id(fir::Instr instr) {
    auto text = fmt::format("{:d}", instr);
    return text.substr(0, text.find(':'));
  }

  static void print_instrs(fir::Function &func, const cost_model::Machine &m) {
    fmt::println("cost model instrs of {}", func.getName());
    for (auto bb : func.get_bbs()) {
      for (auto instr : bb->instructions) {
        cost_model::CostCtx ctx;
        ctx.m = &m;
        cost_model::detect_fold_context(instr, ctx);
        const double lat = cost_model::cost(instr, cost_model::CostKind::Latency, ctx).value;
        const double tput =
            cost_model::cost(instr, cost_model::CostKind::Throughput, ctx).value;
        const double size =
            cost_model::cost(instr, cost_model::CostKind::Size, ctx).value;
        fmt::println("  {}: lat={} tput={} size={}", short_id(instr), lat,
                     tput, size);
      }
    }
    fmt::println("");
  }

  static void print_blocks(fir::Function &func, const cost_model::Machine &m) {
    fmt::println("cost model blocks of {}", func.getName());
    u32 idx = 0;
    for (auto bb : func.get_bbs()) {
      cost_model::BlockThroughputCost bt{bb, m};
      fmt::println("  bb{}: resource={} critpath={} estimate={}", idx++,
                   bt.resource_bound(), bt.critical_path(), bt.estimate());
    }
    fmt::println("");
  }

  static void print_loops(fir::Function &func, const cost_model::Machine &m) {
    fmt::println("cost model loops of {}", func.getName());
    auto &cfg = AnalysisManager::cfg(func);
    auto &dom = AnalysisManager::dom(func);
    LoopInfoAnalysis loops(dom);
    for (auto &loop : loops.info) {
      ScalarEvo evo(cfg, loop);
      cost_model::LoopCostAnalysis lc(cfg, dom, loop, evo, m);
      if (lc.trip_count()) {
        fmt::println(
            "  loop head=bb{} trip={} iter_resource={} chain_latency={} "
            "total={}",
            loop.head, *lc.trip_count(), lc.iter_resource_bound(),
            lc.chain_latency(), lc.total_cost());
      } else {
        fmt::println(
            "  loop head=bb{} trip=unknown iter_resource={} "
            "chain_latency={} total={}",
            loop.head, lc.iter_resource_bound(), lc.chain_latency(),
            lc.total_cost());
      }
    }
    fmt::println("");
  }
};

}  // namespace foptim::optim
