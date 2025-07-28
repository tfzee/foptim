#pragma once
#include <fmt/core.h>

#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "optim/function_pass.hpp"

namespace foptim::optim {

class FuncAnnotator final : public FunctionPass {
 public:
  void apply(fir::Context & /*ctx*/, fir::Function &func) override {
    bool does_read = false;
    bool wont_recurse = true;
    bool does_write = false;

    for (auto bb : func.basic_blocks) {
      for (auto instr : bb->instructions) {
        if (instr->is(fir::InstrType::StoreInstr)) {
          does_write = true;
        }
        if (instr->is(fir::InstrType::LoadInstr)) {
          does_read = true;
        }
        if (instr->is(fir::InstrType::CallInstr)) {
          if (instr->args[0].is_constant() &&
              instr->args[0].as_constant()->is_func()) {
            auto f = instr->args[0].as_constant()->as_func();
            if (!f->no_recurse) {
              wont_recurse = false;
            }
            if (f->mem_read_none) {
            } else if (f->mem_read_only) {
              does_read = true;
            } else if (f->mem_write_only) {
              does_write = true;
            } else {
              does_read = true;
              does_write = true;
            }
          } else {
            wont_recurse = false;
            does_read = true;
            does_write = true;
          }
        }
        if (does_read && does_write && !wont_recurse) {
          break;
        }
      }
      if (does_read && does_write && !wont_recurse) {
        break;
      }
    }
    func.mem_read_none = false;
    func.mem_read_only = false;
    func.mem_write_only = false;
    if (wont_recurse) {
      // dont reset this one since a no recurse function wont be turned into
      // recurse in our stuff atleast for now
      func.no_recurse = true;
    }
    if (does_read && !does_write) {
      func.mem_read_only = true;
    } else if (!does_read && does_write) {
      func.mem_write_only = true;
    } else if (!does_read && !does_write) {
      func.mem_read_none = true;
    }
  }
};
}  // namespace foptim::optim
