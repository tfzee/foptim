#pragma once
#include "ir/IRLocation.hpp"
#include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/function.hpp"
#include "ir/function_ref.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "utils/logging.hpp"

namespace foptim::optim {

struct LiveRange {
  u32 bb;
  u32 start;
  u32 end;

  LiveRange(fir::IRLocation a) {
    if (a.type == fir::IRLocation::LocationType::Instruction) {
      bb = a.bb;
      start = a.instr;
      end = a.instr;
    } else if (a.type == fir::IRLocation::LocationType::BasicBlock) {
      bb = a.bb;
      start = 0;
      // end = 0;
      end = a.func->get_bbs()[bb]->n_instrs();
    } else if (a.type == fir::IRLocation::LocationType::Function) {
      assert(false);
    }
  }

  // NOTE: not including the first
  static LiveRange between_instr(fir::IRLocation start_not_included,
                                 fir::IRLocation end) {
    assert(start_not_included.type ==
           fir::IRLocation::LocationType::Instruction);
    assert(end.type == fir::IRLocation::LocationType::Instruction);
    assert(start_not_included.bb == end.bb);
    assert((void *)start_not_included.func.func == (void *)end.func.func);
    LiveRange res{start_not_included};
    res.start++;
    res.end = end.instr;
    assert(res.start <= res.end);
    return res;
  }

  static LiveRange start_after_instr(fir::IRLocation loc) {
    assert(loc.type == fir::IRLocation::LocationType::Instruction);
    LiveRange res{loc};
    res.start++;
    res.end = loc.func->get_bbs()[res.bb]->n_instrs();
    assert(res.start <= res.end);
    return res;
  }

  static LiveRange end_at_instr(fir::IRLocation loc) {
    assert(loc.type == fir::IRLocation::LocationType::Instruction);
    LiveRange res{loc};
    res.start = 0;
    return res;
  }

  [[nodiscard]] bool overlap(const LiveRange &other) const {
    if (bb != other.bb) {
      return false;
    }
    // if (end == 0 || other.end == 0) {
    //   return true;
    // }
    return start <= other.end && end >= other.start;
  }
};

class LiveVariables {
public:
  LiveVariables(fir::Function &func, CFG &cfg) { update(func, cfg); }

  IRMap<fir::ValueR, IRVec<LiveRange>> live_variables;

  void dump();

  bool isLive(fir::ValueR v, fir::IRLocation location) {
    for (const auto &range : live_variables.at(v)) {
      if (range.overlap(LiveRange{location})) {
        return true;
      }
    }
    return false;
  }

  u32 numLive(fir::IRLocation location) {
    u32 num = 0;
    for (const auto &[val, locs] : live_variables) {
      for (const auto &loc : locs) {
        if (loc.overlap({location})) {
          num++;
        }
      }
    }
    return num;
  }

  bool collide(fir::ValueR v1, fir::ValueR v2) {
    if (!live_variables.contains(v1) || !live_variables.contains(v2)) {
      return true;
    }
    for (const auto &r1 : live_variables.at(v1)) {
      for (const auto &r2 : live_variables.at(v2)) {
        if (r1.overlap(r2)) {
          return true;
        }
      }
    }
    return false;
  }

  static IRMap<fir::ValueR, size_t> setup_values(fir::Function &func) {
    IRMap<fir::ValueR, size_t> values;
    // values.reserve(func.n_instrs() * 2);
    size_t value_id = 0;
    for (const auto &bb : func.get_bbs()) {
      for (u32 arg_id = 0; arg_id < bb->n_args(); arg_id++) {
        auto [_, ins] = values.insert({fir::ValueR{bb, arg_id}, value_id});
        ASSERT(ins);
        value_id++;
      }
      for (auto instr : bb->instructions) {
        if (instr->has_result()) {
          auto [_, ins] = values.insert({fir::ValueR{instr}, value_id});
          ASSERT(ins);
          value_id++;
        }
      }
    }

    return values;
  }

  void update(fir::Function &func, CFG &cfg);
};

} // namespace foptim::optim
