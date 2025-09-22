#include "basic_alias_test.hpp"
#include "ir/basic_block.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::optim {
AliasAnalyis::HeapEntry AliasAnalyis::analyze_impl(fir::ValueR v) {
  if (!v.get_type()->is_ptr()) {
    return HeapEntry{.heap = 0, .offset = {}};
  }
  if (v.is_constant()) {
    auto c = v.as_constant();
    if (c->is_null()) {
      return {.heap = null_h, .offset = {}};
    }
    if (c->is_poison()) {
      return {.heap = poision_h, .offset = {}};
    }
    if (c->is_int() || c->is_float()) {
      return {.heap = 0, .offset = {}};
    }
    if (c->is_global() || c->is_func()) {
      return {.heap = createHeap(staticH_h), .offset = 0};
    }
    fmt::println("{}", v);
    TODO("okak");
  } else if (v.is_instr()) {
    auto i = v.as_instr();
    if (i->is(fir::InstrType::AllocaInstr)) {
      return {.heap = createHeap(local_stack_h), .offset = 0};
    }
    if (i->is(fir::InstrType::SelectInstr)) {
      auto a = analyze(i->args[1]);
      auto b = analyze(i->args[2]);
      return {.heap = meet(a.heap, b.heap), .offset = {}};
    }
    if (i->is(fir::InstrType::LoadInstr)) {
      // TODO: certain things like loading a constant global -> we could check
      // if its a known ref
      // TODO: if its loading from a arg ptr it cant be local
      return {.heap = any_h, .offset = {}};
    }
    if (i->is(fir::InstrType::CallInstr)) {
      // TODO: prob can also optimize this
      return {.heap = any_h, .offset = {}};
    }
    if (i->is(fir::InstrType::BinaryInstr)) {
      auto a = analyze(i->args[0]);
      if (i->subtype == (u32)fir::BinaryInstrSubType::IntAdd &&
          i->args[1].is_constant() && i->args[1].as_constant()->is_int()) {
        return {.heap = a.heap, .offset = i->args[1].as_constant()->as_int()};
      }
      auto b = analyze(i->args[1]);
      // TOOD: implement binary instr stuff like add for offset
      if (b.heap == 0) {
        return {.heap = a.heap, .offset = {}};
      }
      if (a.heap == 0) {
        return {.heap = b.heap, .offset = {}};
      }
      if (a.heap == 0 && b.heap == 0) {
        return {.heap = 0, .offset = {}};
      }
      fmt::println("{}", i);
      TODO("seems illegal");
    }
    fmt::println("{}", v.as_instr());
  } else if (v.is_bb_arg()) {
    auto arg = v.as_bb_arg();
    if (arg->_parent == arg->_parent->get_parent()->get_entry()) {
      return {.heap = argument_h, .offset = {}};
    }
    // TODO: implement meet
    return {.heap = any_h, .offset = {}};
  } else {
    fmt::println("{}", v);
  }
  TODO("okak");
}

}  // namespace foptim::optim
