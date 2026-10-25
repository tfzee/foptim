#pragma once
#include "config/compiler_config.hpp"
#include "ir/function.hpp"
#include "ir/print_ids.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/mem/alias_query.hpp"
#include "optim/analysis/mem/memory_ssa.hpp"
#include "optim/function_pass.hpp"
#include "optim/module_pass.hpp"

namespace foptim::optim {
class PrintFunc final : public FunctionPass {
 public:
  struct Config {
    FString name_match;
  };
  Config config;

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    if (!config.name_match.empty() &&
        !func.getName().contains(config.name_match)) {
      return PreservedAnalysis::all();
    }
    if (ctx.config->debug.print_color) {
      fmt::println("{:cd}", func);
    } else {
      fmt::println("{:d}", func);
    }
    return PreservedAnalysis::all();
  }
};

// Debug pass that dumps analyses, so tests can pin what an analysis answers
// instead of what a transformation does with it. Which analyses is chosen with
// the show option, e.g. PrintAnalysis{show=["memssa", "alias"]} in a pass list.
class PrintAnalysis final : public ModulePass {
 public:
  enum Show : u8 {
    MemSSA = 1U << 0,
    Alias = 1U << 1,
    Provenance = 1U << 2,
    Accesses = 1U << 3,
  };
  struct Config {
    u8 show = 0;
  };
  Config config;

  // MaxAliasAccesses bounds the pairwise alias dump
  static constexpr u32 MaxAliasAccesses = 64;

  // A module pass so the functions come in the order of the module, which lets
  // a test put the CHECK lines of a function inside of it.
  PreservedAnalysis apply(fir::Context &ctx, JobSheduler * /*shed*/,
                          AnalysisManager & /*analyMan*/) override {
    for (auto *func : ctx->storage.functions.all()) {
      if (!func->is_decl()) {
        print(*func);
      }
    }
    return PreservedAnalysis::all();
  }

 private:
  void print(fir::Function &func) const {
    fir::PrintIdsScope id_scope;
    if ((config.show & MemSSA) != 0) {
      print_memssa(func);
    }
    if ((config.show & Accesses) != 0) {
      print_accesses(func);
    }
    if ((config.show & Provenance) != 0) {
      print_provenance(func);
    }
    if ((config.show & Alias) != 0) {
      print_alias(func);
    }
  }

  // "%5" of "%5: i32 = Load(...)"
  static std::string short_id(fir::Instr instr) {
    auto text = fmt::format("{:d}", instr);
    return text.substr(0, text.find(':'));
  }
  static const char *kind_name(MemAccess::Kind k) {
    switch (k) {
      case MemAccess::Kind::Load:
        return "load";
      case MemAccess::Kind::Store:
        return "store";
      case MemAccess::Kind::BarrierRW:
        return "barrier_rw";
      case MemAccess::Kind::BarrierR:
        return "barrier_r";
    }
    return "?";
  }
  static const char *alias_name(AliasResult r) {
    switch (r) {
      case AliasResult::No:
        return "No";
      case AliasResult::May:
        return "May";
      case AliasResult::Partial:
        return "Partial";
      case AliasResult::Must:
        return "Must";
    }
    return "?";
  }

  static void print_memssa(fir::Function &func) {
    auto &mssa = AnalysisManager::memory_ssa(func);
    ASSERT(mssa.verify(AnalysisManager::cfg(func), AnalysisManager::dom(func)));
    AliasQuery aq{AnalysisManager::provenance(func)};
    MemorySSAWalker walker{mssa, AnalysisManager::mem_accesses(func), aq};
    fmt::println("memory ssa of {}\n{}", func.getName(),
                 mssa.to_string(AnalysisManager::cfg(func), &walker));
  }

  // kind, id, size and decoded address of every access
  static void print_accesses(fir::Function &func) {
    fmt::println("accesses of {}", func.getName());
    u32 idx = 0;
    for (const auto &a : AnalysisManager::mem_accesses(func).accesses()) {
      fmt::print("  [{}] ", idx++);
      if (!a.is_load_store()) {
        fmt::println("{} {}", kind_name(a.kind), short_id(a.instr));
        continue;
      }
      const auto &p = a.loc.ptr;
      std::string terms;
      for (const auto &t : p.terms()) {
        terms += fmt::format(" + {} * {}", t.index, t.scale);
      }
      fmt::println("{} {} size={} base={} offset={}{}{}", kind_name(a.kind),
                   short_id(a.instr), a.loc.size, p.base, p.offset, terms,
                   a.ordered ? " ordered" : "");
    }
    fmt::println("");
  }

  static void print_provenance(fir::Function &func) {
    auto &prov = AnalysisManager::provenance(func);
    fmt::println("provenance of {}", func.getName());
    const auto line = [&](fir::ValueR v) {
      fmt::println("  {}:{}{}{}{}{}", v, prov.is_stack(v) ? " stack" : "",
                   prov.is_global(v) ? " global" : "",
                   prov.definitely_null(v) ? " null" : "",
                   prov.non_null(v) ? " nonnull" : " maybe_null",
                   prov.escapes(v) ? " escapes" : "");
    };
    for (const auto &arg : func.get_entry()->args) {
      if (fir::ValueR{arg}.get_type()->is_ptr()) {
        line(fir::ValueR{arg});
      }
    }
    for (const auto bb : func.basic_blocks) {
      for (const auto instr : bb->instructions) {
        if (instr->n_results() == 1 && instr.result_type()->is_ptr()) {
          line(instr.result());
        }
      }
    }
    fmt::println("");
  }

  // alias answer for every pair of loads / stores (the first
  // MaxAliasAccesses of them) and what every barrier does to them, accesses
  // are named by their index in the accesses dump
  static void print_alias(fir::Function &func) {
    AliasQuery aq{AnalysisManager::provenance(func)};
    fmt::println("alias of {}", func.getName());
    const auto &all = AnalysisManager::mem_accesses(func).accesses();
    TVec<u32> ls;
    for (u32 i = 0; i < all.size(); i++) {
      if (all[i].is_load_store() && ls.size() < MaxAliasAccesses) {
        ls.push_back(i);
      }
    }
    for (size_t i = 0; i < ls.size(); i++) {
      for (size_t j = i + 1; j < ls.size(); j++) {
        fmt::println("  {} {}: {}", ls[i], ls[j],
                     alias_name(aq.alias(all[ls[i]].instr, all[ls[j]].instr)));
      }
    }
    for (u32 b = 0; b < all.size(); b++) {
      if (all[b].is_load_store()) {
        continue;
      }
      for (const u32 l : ls) {
        const auto mr = aq.mod_ref(all[b].instr, all[l].loc);
        fmt::println("  {} -> {}: {}", b, l,
                     mr == ModRef::NoModRef ? "none"
                     : mr == ModRef::Ref    ? "ref"
                     : mr == ModRef::Mod    ? "mod"
                                            : "modref");
      }
    }
    fmt::println("");
  }
};
}  // namespace foptim::optim
