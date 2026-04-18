#pragma once
#include <fmt/base.h>
#include <fmt/core.h>

#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/function.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/function_pass.hpp"
#include "utils/arena.hpp"

namespace foptim::optim {

class SLPVectorizer final : public FunctionPass {
 public:
  static constexpr bool debug_print = false;

 public:
  struct SeedInstrData {
    fir::Instr instr;
    fir::ValueR a;
    fir::ValueR b;
  };
  struct SeedBundle {
    fir::ValueR base;
    fir::TypeR type;
    //(base*a + b)
    TVec<SeedInstrData> data;
  };
  class TreeElem {
   public:
    TVec<TreeElem *> children;
    fir::Instr insert_loc;
    u32 n_lanes;

    TreeElem() = default;
    virtual fir::ValueR generate(fir::Context & /*ctx*/,
                                 SeedBundle & /*orig_bundle*/) {
      TODO("UNREACH");
    }
    virtual void dump() { TODO("UNREACH"); }
    // higher = better
    virtual i64 cost() const { TODO("UNREACH"); }
    virtual ~TreeElem() = default;
  };

 private:
  std::pair<SeedInstrData, fir::ValueR> get_storeload_data(
      fir::Instr storeload);

  std::optional<SeedBundle> find_successive_loads(fir::BasicBlock bb,
                                                  size_t instr_id,
                                                  AliasAnalyis &aa);

  std::optional<SeedBundle> find_reduction(fir::BasicBlock bb, size_t instr_id,
                                           AliasAnalyis &aa);

  std::optional<SeedBundle> find_successive_stores(fir::BasicBlock bb,
                                                   size_t instr_id,
                                                   AliasAnalyis &aa);

  void find_seeds(fir::BasicBlock bb, TVec<SeedBundle> &store_bundles,
                  TVec<SeedBundle> &load_bundles,
                  TVec<SeedBundle> &reduction_bundles, AliasAnalyis &aa);

  bool constant_vector_store(fir::Context &ctx, SeedBundle &bundle);

  void continious_vector_store(SeedBundle &bundle, fir::ValueR value);
  bool tree_vectorize(fir::Context &ctx, SeedBundle &bundle,
                      const TVec<SeedBundle> &load_bundles,
                      TreeElem *default_parent = nullptr);
  bool tree_vectorize_reduction(fir::Context &ctx, SeedBundle &bundle,
                                const TVec<SeedBundle> &load_bundles);

 public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedN("SLPVectorizer");
    AliasAnalyis aa{};
    // if (func.name != "_Z19_nettle_aes_encryptjPKjPK9aes_tablemPhPKh") {
    //   return;
    // }
    // fmt::println("{:cd}", func);
    TVec<SeedBundle> store_bundles;
    TVec<SeedBundle> load_bundles;
    TVec<SeedBundle> reduction_bundles;
    for (auto bb : func.basic_blocks) {
      find_seeds(bb, store_bundles, load_bundles, reduction_bundles, aa);
    }
    if (store_bundles.empty() && reduction_bundles.empty()) {
      return;
    }
    if constexpr (debug_print) {
      fmt::println("Stor {} Lod {} Red {}", store_bundles.size(),
                   load_bundles.size(), reduction_bundles.size());
    }

    for (auto &b : store_bundles) {
      bool already_used = false;
      for (const auto &data : b.data) {
        if (!data.instr.is_valid()) {
          already_used = true;
          break;
        }
      }
      if (already_used) {
        continue;
      }
      // if (constant_vector_store(ctx, b)) {
      //   continue;
      // }
      auto stor = utils::TempAlloc<void *>::save();
      if (tree_vectorize(ctx, b, load_bundles)) {
        utils::TempAlloc<void *>::restore(stor);
        continue;
      }
    }
    for (auto &b : reduction_bundles) {
      bool already_used = false;
      for (const auto &data : b.data) {
        if (!data.instr.is_valid()) {
          already_used = true;
          break;
        }
      }
      if (already_used) {
        continue;
      }
      auto stor = utils::TempAlloc<void *>::save();
      if (tree_vectorize_reduction(ctx, b, load_bundles)) {
        utils::TempAlloc<void *>::restore(stor);
        continue;
      }
    }

    // fmt::println("{}:", func);
    // TODO("okak");
  }
};

}  // namespace foptim::optim
