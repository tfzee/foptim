#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/function_pass.hpp"
#include "utils/arena.hpp"
#include <fmt/core.h>

namespace foptim::optim {

class SLPVectorizer final : public FunctionPass {
public:
  struct StoreLoadData {
    fir::Instr instr;
    fir::ValueR a;
    fir::ValueR b;
  };
  struct StoreLoadBundle {
    fir::ValueR base;
    fir::TypeR type;
    //(base*a + b)
    TVec<StoreLoadData> data;
  };

private:
  std::pair<StoreLoadData, fir::ValueR>
  get_storeload_data(fir::Instr storeload) {
    StoreLoadData data;
    data.instr = storeload;
    if (!storeload->args[0].is_instr()) {
      return {data, storeload->args[0]};
    }
    auto arg = storeload->args[0].as_instr();
    if (arg->is(fir::InstrType::BinaryInstr) &&
        arg->subtype == (u32)fir::BinaryInstrSubType::IntAdd) {
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

  std::optional<StoreLoadBundle> find_successive_loads(fir::BasicBlock bb,
                                                       size_t instr_id) {
    StoreLoadBundle curr;
    auto [data, base] = get_storeload_data(bb->instructions[instr_id]);
    curr.base = base;
    curr.type = bb->instructions[instr_id].get_type();
    curr.data.push_back(data);

    for (auto i = instr_id + 1; i < bb->instructions.size(); i++) {
      auto instr = bb->instructions[i];
      if (instr->is(fir::InstrType::LoadInstr) &&
          curr.type == instr.get_type()) {
        auto [sdata, sbase] = get_storeload_data(instr);
        if (curr.base == sbase) {
          curr.data.push_back(sdata);
        }
      } else if (instr->pot_modifies_mem()) {
        // TODO if it writes we could use aliasing to still apply this
        break;
      }
    }
    if (curr.data.size() > 1) {
      return curr;
    }
    return {};
  }

  std::optional<StoreLoadBundle> find_successive_stores(fir::BasicBlock bb,
                                                        size_t instr_id) {
    StoreLoadBundle curr;
    auto [data, base] = get_storeload_data(bb->instructions[instr_id]);
    curr.base = base;
    curr.type = bb->instructions[instr_id].get_type();
    curr.data.push_back(data);

    for (auto i = instr_id + 1; i < bb->instructions.size(); i++) {
      auto instr = bb->instructions[i];
      if (instr->is(fir::InstrType::StoreInstr) &&
          curr.type == instr.get_type()) {
        auto [sdata, sbase] = get_storeload_data(instr);
        if (curr.base == sbase) {
          curr.data.push_back(sdata);
        } else {
          // TODO: could do aliasing check here
          //  only if they alias we need to stop
          break;
        }
      } else if (instr->pot_modifies_mem() || instr->pot_reads_mem()) {
        // TODO if it writes we could use aliasing to still apply this
        break;
      }
    }
    if (curr.data.size() > 1) {
      return curr;
    }
    return {};
  }

  void find_successive_storeloads(fir::BasicBlock bb,
                                  TVec<StoreLoadBundle> &store_bundles,
                                  TVec<StoreLoadBundle> &load_bundles) {
    for (size_t i = 0; i < bb->instructions.size(); i++) {
      if (bb->instructions[i]->is(fir::InstrType::StoreInstr)) {
        auto res = find_successive_stores(bb, i);
        if (res) {
          store_bundles.push_back(res.value());
        }
      } else if (bb->instructions[i]->is(fir::InstrType::LoadInstr)) {
        auto res = find_successive_loads(bb, i);
        if (res) {
          load_bundles.push_back(res.value());
        }
      }
    }
    std::sort(store_bundles.begin(), store_bundles.end(),
              [](const StoreLoadBundle &b1, const StoreLoadBundle &b2) {
                return b1.data.size() > b2.data.size();
              });
    std::sort(load_bundles.begin(), load_bundles.end(),
              [](const StoreLoadBundle &b1, const StoreLoadBundle &b2) {
                return b1.data.size() > b2.data.size();
              });
    // CLEANUP
    for (auto bi = load_bundles.size(); bi > 0; bi--) {
      auto &b = load_bundles[bi - 1];
      {
        auto n_stor = b.data.size();
        if (n_stor != 2 && n_stor != 4 && n_stor != 8 && n_stor != 16) {
          load_bundles.erase(load_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 1 && n_stor % 16 != 0) {
          load_bundles.erase(load_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 2 && n_stor % 8 != 0) {
          load_bundles.erase(load_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 4 && n_stor % 4 != 0) {
          load_bundles.erase(load_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 8 && n_stor % 2 != 0) {
          load_bundles.erase(load_bundles.begin() + bi - 1);
          continue;
        }
        // sort the stores
        std::sort(b.data.begin(), b.data.end(),
                  [](const auto &a, const auto &b) {
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
            load_bundles.erase(load_bundles.begin() + bi - 1);
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
            load_bundles.erase(load_bundles.begin() + bi - 1);
            continue;
          }
        }
      }
    }

    for (auto bi = store_bundles.size(); bi > 0; bi--) {
      auto &b = store_bundles[bi - 1];
      {
        auto n_stor = b.data.size();
        if (n_stor != 2 && n_stor != 4 && n_stor != 8 && n_stor != 16) {
          store_bundles.erase(store_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 1 && n_stor % 16 != 0) {
          store_bundles.erase(store_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 2 && n_stor % 8 != 0) {
          store_bundles.erase(store_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 4 && n_stor % 4 != 0) {
          store_bundles.erase(store_bundles.begin() + bi - 1);
          continue;
        }
        if (b.type->get_size() == 8 && n_stor % 2 != 0) {
          store_bundles.erase(store_bundles.begin() + bi - 1);
          continue;
        }
        // sort the stores
        std::sort(b.data.begin(), b.data.end(),
                  [](const auto &a, const auto &b) {
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

  bool constant_vector_store(fir::Context &ctx, StoreLoadBundle &bundle) {
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

  void continious_vector_store(StoreLoadBundle &bundle, fir::ValueR value);
  bool tree_vectorize(fir::Context &ctx, StoreLoadBundle &bundle,
                      const TVec<StoreLoadBundle> &load_bundles);

public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    (void)ctx;
    (void)func;
    // fmt::println("{}", func);
    TVec<StoreLoadBundle> store_bundles;
    TVec<StoreLoadBundle> load_bundles;
    for (auto bb : func.basic_blocks) {
      find_successive_storeloads(bb, store_bundles, load_bundles);
    }

    for (auto bi = store_bundles.size(); bi > 0; bi--) {
      auto &b = store_bundles[bi - 1];
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

    // fmt::println("{}:", func);
    // TODO("okak");
  }
};

} // namespace foptim::optim
