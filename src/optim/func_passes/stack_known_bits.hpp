#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/cfg.hpp"
#include "utils/bitset.hpp"
#include "utils/logging.hpp"
#include <deque>

namespace foptim::optim {

// propagate and calculate per bb entry/exit via dataflow
// and then do it inside bbs
class StackKnownBits final : public FunctionPass {

  enum StackOffsetResult {
    // could write anywhere on stack
    UnknownLocal,
    // we know where it writes
    KnownLocal,
    // we know it doesnt write to stack
    KnownNonLocal,
  };
  struct CachedKnowledge {
    StackOffsetResult result;
    u64 offset;
  };

  StackOffsetResult get_stack_offset(u64 &offset, fir::ValueR ptr,
                                     TMap<fir::ValueR, CachedKnowledge> &cache);
  void update_call(fir::Instr instr, utils::BitSet<> &new_in_one,
                   utils::BitSet<> &new_in_zero,
                   TMap<fir::ValueR, CachedKnowledge> &cache);
  // returns false if the optimization failed
  bool update_store(fir::Instr instr, utils::BitSet<> &new_in_one,
                    utils::BitSet<> &new_in_zero,
                    TMap<fir::ValueR, CachedKnowledge> &cache);
  void update_load(fir::Instr instr, utils::BitSet<> &new_in_one,
                   utils::BitSet<> &new_in_zero,
                   TMap<fir::ValueR, CachedKnowledge> &cache,
                   TMap<fir::ValueR, i128> &known_load_values);

public:
  void apply(fir::Context &ctx, fir::Function &func) override {
    using namespace foptim::fir;
    ZoneScopedN("StackKnownBits");

    u64 stack_size = 0;
    TMap<ValueR, CachedKnowledge> cache;

    for (auto instr : func.basic_blocks[0]->instructions) {
      if (instr->is(fir::InstrType::AllocaInstr)) {
        auto a1 = instr->get_arg(0);
        if (!a1.is_constant()) {
          fmt::println("Failed dynamic alloca");
          return;
        }
        cache.insert({fir::ValueR{instr}, {KnownLocal, stack_size}});
        stack_size += a1.as_constant()->as_int() * 8;
      }
    }

    fmt::println("Got {} bits\n", stack_size);
    if (stack_size == 0 || stack_size > 4096) {
      return;
    }

    TVec<utils::BitSet<>> exit_known_one;
    TVec<utils::BitSet<>> exit_known_zero;

    exit_known_one.resize(func.n_bbs(), utils::BitSet<>::empty(stack_size));
    exit_known_zero.resize(func.n_bbs(), utils::BitSet<>::empty(stack_size));

    std::deque<u32, utils::TempAlloc<u32>> worklist;
    worklist.push_back(0);
    CFG cfg{func};

    auto new_in_one = utils::BitSet<>::empty(stack_size);
    auto new_in_zero = utils::BitSet<>::empty(stack_size);

    TMap<ValueR, i128> known_load_values;

    while (!worklist.empty()) {
      auto curr = worklist.front();
      worklist.pop_front();

      new_in_one.reset(false);
      new_in_zero.reset(false);
      for (auto p : cfg.bbrs[curr].pred) {
        new_in_one.add(exit_known_one[p]);
        new_in_zero.add(exit_known_zero[p]);
      }

      for (auto instr : cfg.bbrs[curr].bb->instructions) {

        if (instr->is(fir::InstrType::Conversion) &&
            (ConversionSubType)instr->subtype == ConversionSubType::PtrToInt) {
          // TODO: this can be improved depending on the usage
          return;
        }
        if (instr->is(fir::InstrType::CallInstr)) {
          update_call(instr, new_in_one, new_in_zero, cache);
          // fmt::println("CALL\n{}\n{}", new_in_zero, new_in_one);
        } else if (instr->is(fir::InstrType::StoreInstr)) {
          if (!update_store(instr, new_in_one, new_in_zero, cache)) {
            return;
          }
          // fmt::println("STORE\n{}\n{}", new_in_zero, new_in_one);
        } else if (instr->is(fir::InstrType::LoadInstr)) {
          update_load(instr, new_in_one, new_in_zero, cache, known_load_values);
          // fmt::println("LOAD\n{}\n{}", new_in_zero, new_in_one);
        }
      }

      bool changed = false;
      if (new_in_one != exit_known_one[curr]) {
        exit_known_one[curr].assign(new_in_one);
        changed = true;
      }
      if (new_in_zero != exit_known_zero[curr]) {
        exit_known_zero[curr].assign(new_in_zero);
        changed = true;
      }
      if (changed) {
        for (auto succ : cfg.bbrs[curr].succ) {
          worklist.push_back(succ);
        }
      }
    }

    for (auto [l, v] : known_load_values) {
      auto load = l;
      auto val = ctx->get_constant_value(v, l.get_type());
      load.replace_all_uses(fir::ValueR(val));
    }
    // for (size_t i = 0; i < cfg.bbrs.size(); i++) {
    //   fmt::println("{}\n {}\n {}\n", i, exit_known_zero[i],
    //   exit_known_one[i]);
    // }
  }
};
} // namespace foptim::optim
