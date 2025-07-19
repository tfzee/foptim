#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/function_pass.hpp"
#include "utils/arena.hpp"
#include <fmt/core.h>

#include <algorithm>

namespace foptim::optim {

class SLPVectorizer final : public FunctionPass {
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

private:
  std::pair<SeedInstrData, fir::ValueR>
  get_storeload_data(fir::Instr storeload) {
    SeedInstrData data;
    data.instr = storeload;
    if (!storeload->args[0].is_instr()) {
      return {data, storeload->args[0]};
    }
    auto arg = storeload->args[0].as_instr();
    if (arg->is(fir::InstrType::BinaryInstr) &&
        arg->subtype == (u32)fir::BinaryInstrSubType::IntAdd &&
        arg->args[1].is_constant()) {
      data.b = arg->args[1];
      return {data, arg->args[0]};
    }
    if (arg->is(fir::InstrType::BinaryInstr) &&
        arg->subtype == (u32)fir::BinaryInstrSubType::IntMul) {
      if (arg->args[0].is_instr() &&
          arg->args[0].as_instr()->is(fir::InstrType::BinaryInstr) &&
          arg->args[0].as_instr()->subtype ==
              (u32)fir::BinaryInstrSubType::IntAdd) {
        auto sub_val = arg->args[0].as_instr();
        data.a = arg->args[1];
        data.b = sub_val->args[1];
        return {data, sub_val->args[0]};
      }
      data.a = arg->args[1];
      return {data, arg->args[0]};
    }
    return {data, storeload->args[0]};
  }

  std::optional<SeedBundle>
  find_successive_loads(fir::BasicBlock bb, size_t instr_id, AliasAnalyis &aa) {
    SeedBundle curr;
    auto [data, base] = get_storeload_data(bb->instructions[instr_id]);
    curr.base = base;
    curr.type = bb->instructions[instr_id].get_type();
    curr.data.push_back(data);

    TVec<fir::Instr> pot_aliasing_stores;

    for (auto i = instr_id + 1; i < bb->instructions.size(); i++) {
      auto instr = bb->instructions[i];
      if (instr->is(fir::InstrType::LoadInstr) &&
          curr.type == instr.get_type()) {
        auto [sdata, sbase] = get_storeload_data(instr);
        if (curr.base == sbase) {
          curr.data.push_back(sdata);
        }
      } else if (instr->is(fir::InstrType::StoreInstr)) {
        pot_aliasing_stores.push_back(instr);
      } else if (instr->pot_modifies_mem()) {
        // TODO if it writes we could use aliasing to still apply this
        break;
      }
    }
    // if we alias with anything just abort for now
    // TODO: could just remove the aliasing loads from the set
    for (auto pot_stor : pot_aliasing_stores) {
      for (auto &load : curr.data) {
        if (aa.alias(pot_stor->args[0], load.instr->args[0]) !=
            AliasAnalyis::AAResult::NoAlias) {
          return {};
        }
      }
    }
    if (curr.data.size() > 1) {
      return curr;
    }
    return {};
  }

  std::optional<SeedBundle> find_reduction(fir::BasicBlock bb, size_t instr_id,
                                           AliasAnalyis &aa) {
    SeedBundle curr;
    (void)bb;
    (void)aa;
    (void)instr_id;
    auto base_instr = bb->instructions[instr_id];
    auto subtype = base_instr->subtype;
    if (!base_instr->is(fir::InstrType::BinaryInstr) ||
        (subtype != (u32)fir::BinaryInstrSubType::IntAdd &&
         subtype != (u32)fir::BinaryInstrSubType::FloatAdd)) {
      return {};
    }
    // fmt::println("MAYBE REDUCTION BUNDLE {}", bb->instructions[instr_id]);
    return {};
  }

  std::optional<SeedBundle> find_successive_stores(fir::BasicBlock bb,
                                                   size_t instr_id,
                                                   AliasAnalyis &aa) {
    SeedBundle curr;
    auto [data, base] = get_storeload_data(bb->instructions[instr_id]);
    curr.base = base;
    curr.type = bb->instructions[instr_id].get_type();
    curr.data.push_back(data);

    // fmt::print("===============Col store================\n{}\n",
    //            bb->instructions[instr_id]);
    for (auto i = instr_id + 1; i < bb->instructions.size(); i++) {
      auto instr = bb->instructions[i];
      if (instr->is(fir::InstrType::StoreInstr) &&
          curr.type == instr.get_type()) {
        auto [sdata, sbase] = get_storeload_data(instr);
        // fmt::println("{} =?= {} => {}\n", curr.base, sbase, curr.base ==
        // sbase);
        if (curr.base == sbase) {
          curr.data.push_back(sdata);
        } else if (aa.alias(curr.base, sbase) !=
                   AliasAnalyis::AAResult::NoAlias) {
          // fmt::println("Failed aliasing store {}\n", instr);
          break;
        }
      } else if (instr->is(fir::InstrType::LoadInstr)) {
        // this load could load one of the previous stores
        //  if that is the case we cannot simply transform it into vector code
        //  since it would need to be placed before this load
        // TODO: we could transform it but leave the store that is associated
        // to this load alive to keep it correct? (would need to check if
        // thats even worth it)
        bool might_alias = false;
        for (const auto &s : curr.data) {
          if (aa.alias(instr->args[0], s.instr->args[0]) !=
              AliasAnalyis::AAResult::NoAlias) {
            might_alias = true;
            break;
          }
        }
        if (might_alias) {
          // fmt::println("Failed aliasing load {}\n", instr);
          break;
        }
      } else if (instr->pot_modifies_mem() || instr->pot_reads_mem()) {
        // fmt::println("Failed pot aliasing operation {}\n", instr);
        // TODO if it writes we could use aliasing to still apply this
        break;
      }
    }
    if (curr.data.size() > 1) {
      return curr;
    }
    return {};
  }

  void find_seeds(fir::BasicBlock bb, TVec<SeedBundle> &store_bundles,
                  TVec<SeedBundle> &load_bundles,
                  TVec<SeedBundle> &reduction_bundles, AliasAnalyis &aa) {
    // collect store and reduction seed bundles
    //  also collect load bundles since that simplifiies later one checking if a
    //  load is valid to vectorize
    for (size_t i = 0; i < bb->instructions.size(); i++) {
      if (bb->instructions[i]->is(fir::InstrType::StoreInstr)) {
        auto res = find_successive_stores(bb, i, aa);
        if (res) {
          store_bundles.push_back(res.value());
        }
      } else if (bb->instructions[i]->is(fir::InstrType::LoadInstr)) {
        auto res = find_successive_loads(bb, i, aa);
        if (res) {
          load_bundles.push_back(res.value());
        }
      }
      (void)reduction_bundles;
      // } else {
      //   auto res = find_reduction(bb, i, aa);
      //   if (res) {
      //     fmt::println("FOUND");
      //     fmt::println("LEN {} AT {} ", res.value().data.size(),
      //                  res.value().base);
      //     TODO("okak");
      //     reduction_bundles.push_back(res.value());
      //   }
      // }
    }
    std::ranges::sort(store_bundles,
                      [](const SeedBundle &b1, const SeedBundle &b2) {
                        return b1.data.size() > b2.data.size();
                      });
    std::ranges::sort(load_bundles,
                      [](const SeedBundle &b1, const SeedBundle &b2) {
                        return b1.data.size() > b2.data.size();
                      });
    // fmt::println("{}  {}", store_bundles.size(), load_bundles.size());
    // CLEANUP
    for (auto bi = load_bundles.size(); bi > 0; bi--) {
      auto &b = load_bundles[bi - 1];
      {
        auto n_stor = b.data.size();
        if (n_stor != 2 && n_stor != 4 && n_stor != 8 && n_stor != 16 &&
            n_stor != 32) {
          load_bundles.erase(load_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 1 && (n_stor % 16 != 0 || n_stor % 32 != 0)) {
          load_bundles.erase(load_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 2 && (n_stor % 8 != 0 || n_stor % 16 != 0)) {
          load_bundles.erase(load_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 4 && (n_stor % 4 != 0 || n_stor % 8 != 0)) {
          load_bundles.erase(load_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 8 && (n_stor % 2 != 0 || n_stor % 4 != 0)) {
          load_bundles.erase(load_bundles.begin() + bi - 1);
          continue;
        }
        // sort the stores
        std::ranges::sort(b.data, [](const auto &a, const auto &b) {
          i128 av = 0;
          i128 bv = 0;
          if (a.b.is_constant() && a.b.as_constant()->is_int()) {
            av = a.b.as_constant()->as_int();
          }
          if (b.b.is_constant() && b.b.as_constant()->is_int()) {
            bv = b.b.as_constant()->as_int();
          }
          return av < bv;
        });
        // check if continious
        {
          TVec<i128> offsets;
          // collect constant offsets
          bool failed = false;
          for (const auto &data : b.data) {
            // fmt::println("GOT {} => {} {}", data.instr, data.a, data.b);
            if (!data.a.is_invalid() ||
                (!data.b.is_invalid() &&
                 (!data.b.is_constant() || !data.b.as_constant()->is_int()))) {
              failed = true;
              break;
            }
            i128 consti = 0;
            if (data.b.is_constant()) {
              consti = data.b.as_constant()->as_int();
            }
            offsets.push_back(consti);
          }
          if (failed) {
            load_bundles.erase(load_bundles.begin() + bi - 1);
            fmt::println("skip constant");
            continue;
          }

          // check they are continious
          for (size_t i = 1; i < offsets.size(); i++) {
            if (offsets[i] - offsets[i - 1] != b.type->get_size()) {
              failed = true;
              break;
            }
          }
          if (failed) {
            fmt::println("skip cont");
            load_bundles.erase(load_bundles.begin() + bi - 1);
            continue;
          }
        }
      }
    }
    // fmt::println("{}  {}", store_bundles.size(), load_bundles.size());

    for (auto bi = store_bundles.size(); bi > 0; bi--) {
      auto &b = store_bundles[bi - 1];
      {
        auto n_stor = b.data.size();
        if (n_stor != 2 && n_stor != 4 && n_stor != 8 && n_stor != 16 &&
            n_stor != 32) {
          // just cut it off 4head
          if (n_stor > 32) {
            b.data.resize(32);
          } else if (n_stor > 16) {
            b.data.resize(16);
          } else if (n_stor > 8) {
            b.data.resize(8);
          } else if (n_stor > 4) {
            b.data.resize(4);
          } else if (n_stor > 2) {
            b.data.resize(2);
          } else {
            store_bundles.erase(store_bundles.begin() + bi - 1);
            continue;
          }
        }
        if (b.type->get_size() == 1 && (n_stor % 16 != 0 || n_stor % 32 != 0)) {
          store_bundles.erase(load_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 2 && (n_stor % 8 != 0 || n_stor % 16 != 0)) {
          store_bundles.erase(store_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 4 && (n_stor % 4 != 0 || n_stor % 8 != 0)) {
          store_bundles.erase(store_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 8 && (n_stor % 2 != 0 || n_stor % 4 != 0)) {
          store_bundles.erase(store_bundles.begin() + bi - 1);
          continue;
        }
        // sort the stores
        std::ranges::sort(b.data, [](const auto &a, const auto &b) {
          i128 av = 0;
          i128 bv = 0;
          if (a.b.is_constant() && a.b.as_constant()->is_int()) {
            av = a.b.as_constant()->as_int();
          }
          if (b.b.is_constant() && b.b.as_constant()->is_int()) {
            bv = b.b.as_constant()->as_int();
          }
          return av < bv;
        });
        // check if continious
        {
          TVec<i128> offsets;
          // collect constant offsets
          bool failed = false;
          for (const auto &data : b.data) {
            if (!data.a.is_invalid() ||
                (!data.b.is_invalid() &&
                 (!data.b.is_constant() || !data.b.as_constant()->is_int()))) {
              failed = true;
              break;
            }
            i128 consti = 0;
            if (data.b.is_constant()) {
              consti = data.b.as_constant()->as_int();
            }
            offsets.push_back(consti);
          }
          if (failed) {
            store_bundles.erase(store_bundles.begin() + bi - 1);
            continue;
          }

          // check they are continious
          for (size_t i = 1; i < offsets.size(); i++) {
            if (offsets[i] - offsets[i - 1] != b.type->get_size()) {
              failed = true;
              break;
            }
          }
          if (failed) {
            store_bundles.erase(store_bundles.begin() + bi - 1);
            continue;
          }
        }
      }
    }
    fmt::println("{}  {}", store_bundles.size(), load_bundles.size());

    // // TODO: improve
    // //  any store set thats a subset of another can be deleted
    // {
    //   for (size_t ip1 = store_bundles.size(); ip1 > 0; ip1--) {
    //     auto &curr_b = store_bundles[ip1 - 1];
    //     bool is_subset = false;
    //     for (size_t i = 0; i < ip1 - 1; i++) {
    //       const auto &test_b = store_bundles[i];
    //       if (curr_b.base != test_b.base || curr_b.type != test_b.type) {
    //         continue;
    //       }
    //       is_subset = true;
    //       break;
    //     }
    //     if (is_subset) {
    //       store_bundles.erase(store_bundles.begin() + ip1 - 1);
    //     }
    //   }
    // }
  }

  bool constant_vector_store(fir::Context &ctx, SeedBundle &bundle) {
    IRVec<fir::ConstantValueR> args;
    for (const auto &data : bundle.data) {
      if (!data.instr->args[1].is_constant()) {
        return false;
      }
    }
    for (const auto &b : bundle.data) {
      args.push_back(b.instr->args[1].as_constant());
    }
    auto constant_data = ctx->get_constant_value(
        std::move(args), ctx->get_vec_type(bundle.type, bundle.data.size()));
    continious_vector_store(bundle, fir::ValueR{constant_data});
    return true;
  }

  void continious_vector_store(SeedBundle &bundle, fir::ValueR value);
  bool tree_vectorize(fir::Context &ctx, SeedBundle &bundle,
                      const TVec<SeedBundle> &load_bundles);

public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedN("SLPVectorizer");
    (void)ctx;
    (void)func;
    AliasAnalyis aa{};
    if (func.name != "_ZL14benchmark_bodyi") {
      return;
    }
    fmt::println("{:cd}", func);
    TVec<SeedBundle> store_bundles;
    TVec<SeedBundle> load_bundles;
    TVec<SeedBundle> reduction_bundles;
    for (auto bb : func.basic_blocks) {
      find_seeds(bb, store_bundles, load_bundles, reduction_bundles, aa);
    }
    fmt::println("Stor {} Lod {}", store_bundles.size(), load_bundles.size());

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
      // TODO("okak");
    }

    // fmt::println("{}:", func);
    // TODO("okak");
  }
};

} // namespace foptim::optim
