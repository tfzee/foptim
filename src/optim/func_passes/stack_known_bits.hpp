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

  // TODO: armotize with a loopup table to cache results
  StackOffsetResult
  get_stack_offset(u64 &offset, fir::ValueR ptr,
                   TMap<fir::ValueR, CachedKnowledge> &cache) {
    using namespace foptim::fir;
    offset = 0;
    if (cache.contains(ptr)) {
      auto res = cache.at(ptr);
      offset = res.offset;
      return res.result;
    }

    if (ptr.is_constant()) {
      return StackOffsetResult::KnownNonLocal;
    }
    if (ptr.is_bb_arg()) {
      auto arg_parent = ptr.as_bb_arg()->get_parent();
      auto entry_block = arg_parent->get_parent()->get_entry();
      return arg_parent == entry_block ? StackOffsetResult::KnownNonLocal
                                       : StackOffsetResult::UnknownLocal;
      // return StackOffsetResult::UnknownLocal;
    }
    if (ptr.is_instr()) {
      auto ptr_instr = ptr.as_instr();
      if (ptr_instr->is(fir::InstrType::AllocaInstr)) {
        fmt::println("Cache should have handled this\n");
        UNREACH();
      }
      if (ptr_instr->is(fir::InstrType::LoadInstr)) {
        // u64 sub_offset = 0;
        // auto sub_result = get_stack_offset(sub_offset, ptr_instr->args[0]);
        // if (sub_result == UnknownLocal || sub_result == KnownLocal) {
        // }
        return StackOffsetResult::UnknownLocal;
      }
      if (ptr_instr->is(fir::InstrType::CallInstr)) {
        for (auto arg : ptr_instr->args) {
          if (arg.get_type()->is_ptr()) {
            u64 sub_offset = 0;
            auto sub_result = get_stack_offset(sub_offset, arg, cache);
            if (!cache.contains(arg)) {
              cache.insert({arg, {sub_result, sub_offset}});
            }
            if (sub_result == UnknownLocal || sub_result == KnownLocal) {
              return StackOffsetResult::UnknownLocal;
            }
          }
        }
        return StackOffsetResult::KnownNonLocal;
      }
      if (ptr_instr->is(fir::InstrType::BinaryInstr) &&
          ((BinaryInstrSubType)ptr_instr->subtype ==
           BinaryInstrSubType::IntAdd)) {
        u64 sub_offset = 0;
        auto sub_result =
            get_stack_offset(sub_offset, ptr_instr->args[0], cache);
        if (!cache.contains(ptr_instr->args[0])) {
          cache.insert({ptr_instr->args[0], {sub_result, sub_offset}});
        }
        if (sub_result == UnknownLocal) {
          return UnknownLocal;
        }
        if (sub_result == KnownLocal) {
          if (ptr_instr->args[1].is_constant() &&
              ptr_instr->args[1].as_constant()->is_int()) {
            auto value = ptr_instr->args[1].as_constant()->as_int();
            offset += sub_offset + value;
            return KnownLocal;
          }
          return UnknownLocal;
        }
        // just assume the worst
        return StackOffsetResult::UnknownLocal;
      }
      fmt::println("{}", ptr_instr);
    }
    fmt::println("{}", ptr);
    TODO("impl?");
    return StackOffsetResult::UnknownLocal;
  }

public:
  void apply(fir::Context & /*unused*/, fir::Function &func) override {
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

    while (!worklist.empty()) {
      auto curr = worklist.front();
      worklist.pop_front();

      // FIXME: dont realloc move out of loop
      auto new_in_one = utils::BitSet<>::empty(stack_size);
      auto new_in_zero = utils::BitSet<>::empty(stack_size);
      for (auto p : cfg.bbrs[curr].pred) {
        new_in_one.add(exit_known_one[p]);
        new_in_zero.add(exit_known_zero[p]);
      }

      for (auto instr : cfg.bbrs[curr].bb->instructions) {
        // if we got a store we need to figure out the value we store
        //  annd if we store to stack and where
        if (instr->is(fir::InstrType::CallInstr)) {
          for (auto arg : instr->args) {
            if (arg.get_type()->is_ptr()) {
              u64 offset = 0;
              auto result = get_stack_offset(offset, arg, cache);
              if (result == StackOffsetResult::KnownLocal ||
                  result == StackOffsetResult::UnknownLocal) {
                new_in_one.reset(false);
                new_in_zero.reset(false);
                break;
              }
            }
          }
        }
        if (instr->is(fir::InstrType::StoreInstr)) {
          u64 offset = 0;
          auto result = get_stack_offset(offset, instr->args[0], cache);
          if (result == StackOffsetResult::KnownLocal ||
              result == StackOffsetResult::UnknownLocal) {

            if (instr->args[1].is_constant() &&
                (instr->args[1].as_constant()->is_int() ||
                 instr->args[1].as_constant()->is_float())) {
              u64 value = 0;
              bool is_int = instr->args[1].as_constant()->is_int();
              bool is_float = instr->args[1].as_constant()->is_float();
              if (is_int) {
                value = instr->args[1].as_constant()->as_int();
              } else if (is_float) {
                value = std::bit_cast<u64>(
                    instr->args[1].as_constant()->as_float());
              } else {
                UNREACH();
              }
              if (result == StackOffsetResult::KnownLocal) {
                auto size = instr->get_type()->get_size() * 8;
                u64 mask = 0;
                if (size == 64) {
                  mask = ~mask;
                } else {
                  mask = ((1 << size) - 1);
                }
                new_in_one.set(offset, size, value & mask);
                new_in_zero.set(offset, size, (~value) & mask);
              } else if (result == StackOffsetResult::UnknownLocal &&
                         value == 0 && is_int) {
                // need to handle seperatly because of -0 in floats
                new_in_one.reset(false);
              } else if (result == StackOffsetResult::UnknownLocal) {
                // TODO theres edgecases to improve this
                new_in_one.reset(false);
                new_in_zero.reset(false);
              }
              continue;
            }

            // we dont know what we write so just reset the bits accordingly
            if (result == StackOffsetResult::KnownLocal) {
              auto size = instr->get_type()->get_size() * 8;
              new_in_one.set(offset, size, 0);
              new_in_zero.set(offset, size, 0);
            }
            if (result == StackOffsetResult::UnknownLocal) {
              new_in_one.reset(false);
              new_in_zero.reset(false);
            }
          }
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

    for (size_t i = 0; i < cfg.bbrs.size(); i++) {
      fmt::println("{}\n {}\n {}\n", i, exit_known_zero[i], exit_known_one[i]);
    }
  }
};
} // namespace foptim::optim
