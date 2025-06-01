#pragma once
#include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value.hpp"
#include "ir/function.hpp"
#include "ir/value.hpp"

namespace foptim::fir::intepreter {

struct State {
  TMap<ValueR, ConstantValue> vals;

  void set_value(ValueR v, ConstantValue cv) { vals.insert_or_assign(v, cv); }
  std::optional<ConstantValue> try_get(ValueR v) {
    if (vals.contains(v)) {
      return vals.at(v);
    }
    return {};
  }
};
struct InstrPointer {
  Function *func;
  u32 bb_id;
  u32 instr_id;
};

class Interpreter {
  State state;
  InstrPointer ip;

public:
  Interpreter(Function *func) : state(), ip({func, 0, 0}) {};
  Interpreter(Function *func, u32 bb_id) : state(), ip({func, bb_id, 0}) {};
  Interpreter(Function *func, u32 bb_id, u32 instr_id)
      : state(), ip({func, bb_id, instr_id}) {};

  void dump_state();
  // returns true if and only if the instruction got correctly interpreted
  bool step_instr();
  // returns true if and only if the bb got correctly interpreted till the end
  bool step_till_end_of_bb();
  // bool interpret(Function *func);
  void set(ValueR r, ConstantValue cv);
  InstrPointer get_ip();
  std::optional<ConstantValue> get_value(fir::ValueR v);
  const TMap<ValueR, ConstantValue> &get_values() const { return state.vals; }
};

} // namespace foptim::fir::intepreter
