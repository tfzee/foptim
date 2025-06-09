#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {

class SLPVectorizer final : public FunctionPass {
  struct StoreData {
    fir::Instr instr;
    fir::ValueR a;
    fir::ValueR b;
  };
  struct StoreBundle {
    fir::ValueR base;
    fir::TypeR type;
    //(base*a + b)
    TVec<StoreData> data;
  };

  std::pair<StoreData, fir::ValueR> get_store_data(fir::Instr store) {
    StoreData data;
    data.instr = store;
    if (!store->args[0].is_instr()) {
      return {data, store->args[0]};
    }
    auto arg = store->args[0].as_instr();
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
    return {data, store->args[0]};
  }

  std::optional<StoreBundle> find_successive_stores(fir::BasicBlock bb,
                                                    size_t instr_id) {
    StoreBundle curr;
    auto [data, base] = get_store_data(bb->instructions[instr_id]);
    curr.base = base;
    curr.type = bb->instructions[instr_id].get_type();
    curr.data.push_back(data);

    for (auto i = instr_id + 1; i < bb->instructions.size(); i++) {
      auto instr = bb->instructions[i];
      if (instr->is(fir::InstrType::StoreInstr) &&
          curr.type == instr.get_type()) {
        auto [sdata, sbase] = get_store_data(instr);
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
  void find_successive_stores(fir::BasicBlock bb, TVec<StoreBundle> &bundles) {
    for (size_t i = 0; i < bb->instructions.size(); i++) {
      if (bb->instructions[i]->is(fir::InstrType::StoreInstr)) {
        auto res = find_successive_stores(bb, i);
        if (res) {
          bundles.push_back(res.value());
        }
      }
    }
  }

  bool constant_vector_store(fir::Context &ctx, StoreBundle &bundle) {
    (void)ctx;
    auto n_stor = bundle.data.size();

    if (n_stor != 2 && n_stor != 4 && n_stor != 8 && n_stor != 16) {
      return false;
    }
    if (bundle.type->get_size() == 1 && n_stor % 16 != 0) {
      return false;
    }
    if (bundle.type->get_size() == 2 && n_stor % 8 != 0) {
      return false;
    }
    if (bundle.type->get_size() == 4 && n_stor % 4 != 0) {
      return false;
    }
    if (bundle.type->get_size() == 8 && n_stor % 2 != 0) {
      return false;
    }

    // sort the stores
    std::sort(bundle.data.begin(), bundle.data.end(),
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

    TVec<i128> offsets;
    // collect constant offsets
    for (const auto &data : bundle.data) {
      if (!data.instr->args[1].is_constant()) {
        return false;
      }
      if (!data.a.is_invalid() ||
          (!data.b.is_invalid() &&
           (!data.b.is_constant() || !data.b.as_constant()->is_int()))) {
        return false;
      }
      i128 consti = 0;
      if (data.b.is_constant()) {
        consti = data.b.as_constant()->as_int();
      }
      offsets.push_back(consti);
    }

    // check they are continious
    for (size_t i = 1; i < offsets.size(); i++) {
      if (offsets[i] - offsets[i - 1] != bundle.type->get_size()) {
        return false;
      }
    }

    size_t last_indx = 0;
    fir::Instr last_instr;
    for (const auto &data : bundle.data) {
      auto bb = data.instr->parent;
      for (size_t i = 0; i < bb->instructions.size(); i++) {
        if (bb->instructions[i] == data.instr) {
          if (i > last_indx) {
            last_indx = i;
            last_instr = data.instr;
          }
          break;
        }
      }
    }

    fir::Builder bb{last_instr};
    IRVec<fir::ConstantValueR> args;
    for (const auto &b : bundle.data) {
      args.push_back(b.instr->args[1].as_constant());
    }
    auto constant_data = ctx->get_constant_value(
        std::move(args), ctx->get_vec_type(bundle.type, bundle.data.size()));
    bb.build_store(bundle.data[0].instr->args[0], fir::ValueR{constant_data});
    for (auto &b : bundle.data) {
      b.instr.destroy();
    }
    return true;
  }

public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    (void)ctx;
    (void)func;
    // fmt::println("{}", func);
    TVec<StoreBundle> bundles;
    for (auto bb : func.basic_blocks) {
      find_successive_stores(bb, bundles);
    }
    std::sort(bundles.begin(), bundles.end(),
              [](const StoreBundle &b1, const StoreBundle &b2) {
                return b1.data.size() > b2.data.size();
              });

    // TODO: improve
    //  any set thats a subset of another can be deleted
    {
      for (size_t ip1 = bundles.size(); ip1 > 0; ip1--) {
        auto &curr_b = bundles[ip1 - 1];
        bool is_subset = false;
        for (size_t i = 0; i < ip1 - 1; i++) {
          const auto &test_b = bundles[i];
          if (curr_b.base != test_b.base || curr_b.type != test_b.type) {
            continue;
          }
          is_subset = true;
          break;
        }
        if (is_subset) {
          bundles.erase(bundles.begin() + ip1 - 1);
        }
      }
    }

    // basic storing a constant vector
    for (auto b : bundles) {
      if (constant_vector_store(ctx, b)) {
        continue;
      }
      fmt::println("{}:", b.base);
      for (auto s : b.data) {
        fmt::println("  * {} + {} = {}", s.a, s.b, s.instr->args[1]);
      }
    }

    // fmt::println("{}:", func);
    // TODO("okak");
  }
};

} // namespace foptim::optim
