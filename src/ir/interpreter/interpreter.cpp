#include "interpreter.hpp"

#include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"

namespace foptim::fir::intepreter {

namespace {

std::optional<ConstantValue> get_constant_value(ValueR val, State &st) {
  if (val.is_constant()) {
    auto c = val.as_constant();
    return *c.get_raw_ptr();
  }
  return st.try_get(val);
}

// integers are kept sign extended to their bit width
i128 wrap_to_type(i128 v, TypeR ty) {
  if (!ty->is_int()) {
    return v;
  }
  const u32 bw = ty->get_bitwidth();
  if (bw >= 128) {
    return v;
  }
  const u128 mask = (static_cast<u128>(1) << bw) - 1;
  u128 u = static_cast<u128>(v) & mask;
  if (((u >> (bw - 1)) & 1) != 0) {
    u |= ~mask;
  }
  return static_cast<i128>(u);
}

bool interpret_branch(Instr instr, State &st, InstrPointer &ip) {
  auto &target_bb = instr->bbs[0];
  for (size_t i = 0; i < target_bb.args.size(); i++) {
    auto v = get_constant_value(target_bb.args[i], st);
    if (!v) {
      return false;
    }
    st.set_value(fir::ValueR(target_bb.bb->args[i]), v.value());
  }

  ip.bb_id = ip.func->bb_id(target_bb.bb);
  ip.instr_id = 0;
  return true;
}

bool interpret_cbranch(Instr instr, State &st, InstrPointer &ip) {
  auto input_val = get_constant_value(instr->args[0], st);
  if (!input_val) {
    return false;
  }
  auto target_bb_id = input_val->as_int() != 0 ? 0 : 1;
  auto &target_bb = instr->bbs[target_bb_id];
  for (size_t i = 0; i < target_bb.args.size(); i++) {
    auto v = get_constant_value(target_bb.args[i], st);
    if (!v) {
      return false;
    }
    st.set_value(fir::ValueR(target_bb.bb->args[i]), v.value());
  }

  ip.bb_id = ip.func->bb_id(target_bb.bb);
  ip.instr_id = 0;
  return true;
}

bool interpret_binary_expr(Instr instr, State &st, InstrPointer &ip) {
  auto v1 = get_constant_value(instr->args[0], st);
  auto v2 = get_constant_value(instr->args[1], st);
  if (!v1 || !v2) {
    return false;
  }
  switch (static_cast<BinaryInstrSubType>(instr->subtype)) {
    case BinaryInstrSubType::IntAdd:
      st.set_value(ValueR(instr), ConstantValue{wrap_to_type(v1->as_int() + v2->as_int(), instr.get_type()),
                                                instr.get_type()});
      break;
    case BinaryInstrSubType::IntMul:
      st.set_value(ValueR(instr), ConstantValue{wrap_to_type(v1->as_int() * v2->as_int(), instr.get_type()),
                                                instr.get_type()});
      break;
    case BinaryInstrSubType::IntSub:
      st.set_value(ValueR(instr), ConstantValue{wrap_to_type(v1->as_int() - v2->as_int(), instr.get_type()),
                                                instr.get_type()});
      break;
    case BinaryInstrSubType::FloatAdd:
      if (instr.get_type()->as_float() == 32) {
        st.set_value(ValueR(instr), ConstantValue{v1->as_f32() + v2->as_f32(),
                                                  instr.get_type()});
      } else {
        st.set_value(ValueR(instr), ConstantValue{v1->as_f64() + v2->as_f64(),
                                                  instr.get_type()});
      }
      break;
    case BinaryInstrSubType::FloatMul:
      if (instr.get_type()->as_float() == 32) {
        st.set_value(ValueR(instr), ConstantValue{v1->as_f32() * v2->as_f32(),
                                                  instr.get_type()});
      } else {
        st.set_value(ValueR(instr), ConstantValue{v1->as_f64() * v2->as_f64(),
                                                  instr.get_type()});
      }
      break;
    case BinaryInstrSubType::And:
      st.set_value(ValueR(instr), ConstantValue{wrap_to_type(v1->as_int() & v2->as_int(), instr.get_type()),
                                                instr.get_type()});
      break;
    case BinaryInstrSubType::Or:
      st.set_value(ValueR(instr), ConstantValue{wrap_to_type(v1->as_int() | v2->as_int(), instr.get_type()),
                                                instr.get_type()});
      break;
    case BinaryInstrSubType::Xor:
      st.set_value(ValueR(instr),
                   ConstantValue{wrap_to_type(v1->as_int() ^ v2->as_int(),
                                              instr.get_type()),
                                 instr.get_type()});
      break;
    case BinaryInstrSubType::Shl:
    case BinaryInstrSubType::Shr:
    case BinaryInstrSubType::AShr: {
      // out of range shifts are poison, do not evaluate them
      if (!instr.get_type()->is_int()) {
        return false;
      }
      const u32 bw = instr.get_type()->get_bitwidth();
      const i128 amt = v2->as_int();
      if (bw > 64 || amt < 0 || amt >= bw) {
        return false;
      }
      const u128 mask = (static_cast<u128>(1) << bw) - 1;
      const u128 uv = static_cast<u128>(v1->as_int()) & mask;
      i128 res;
      switch (static_cast<BinaryInstrSubType>(instr->subtype)) {
      case BinaryInstrSubType::Shl:
        res = static_cast<i128>(uv << amt);
        break;
      case BinaryInstrSubType::Shr:
        res = static_cast<i128>(uv >> amt);
        break;
      default:
        res = wrap_to_type(v1->as_int(), instr.get_type()) >> amt;
        break;
      }
      st.set_value(ValueR(instr), ConstantValue{wrap_to_type(res, instr.get_type()),
                                                instr.get_type()});
      break;
    }
    case BinaryInstrSubType::IntSRem:
    case BinaryInstrSubType::IntSDiv:
    case BinaryInstrSubType::IntUDiv: {
      if (!instr.get_type()->is_int()) {
        return false;
      }
      const u32 bw = instr.get_type()->get_bitwidth();
      if (bw > 64) {
        return false;
      }
      const i128 a = wrap_to_type(v1->as_int(), instr.get_type());
      const i128 b = wrap_to_type(v2->as_int(), instr.get_type());
      if (b == 0) {
        return false;
      }
      i128 res;
      if (static_cast<BinaryInstrSubType>(instr->subtype) ==
          BinaryInstrSubType::IntUDiv) {
        const u128 mask = (static_cast<u128>(1) << bw) - 1;
        res = static_cast<i128>((static_cast<u128>(a) & mask) /
                                (static_cast<u128>(b) & mask));
      } else if (static_cast<BinaryInstrSubType>(instr->subtype) ==
                 BinaryInstrSubType::IntSDiv) {
        res = a / b;
      } else {
        res = a % b;
      }
      st.set_value(ValueR(instr), ConstantValue{wrap_to_type(res, instr.get_type()),
                                                instr.get_type()});
      break;
    }
    default:
      return false;
    case BinaryInstrSubType::INVALID:
      TODO("INVALID");
  }
  ip.instr_id++;
  return true;
}

bool interpret_icmp(Instr instr, State &st, InstrPointer &ip) {
  auto v1 = get_constant_value(instr->args[0], st);
  auto v2 = get_constant_value(instr->args[1], st);
  if (!v1 || !v2) {
    return false;
  }
  switch (static_cast<ICmpInstrSubType>(instr->subtype)) {
    case ICmpInstrSubType::SGE:
      st.set_value(ValueR(instr), ConstantValue{(v1->as_int() >= v2->as_int())
                                                    ? ~static_cast<i128>(0)
                                                    : 0,
                                                instr->get_type()});
      break;
    case ICmpInstrSubType::SGT:
      st.set_value(ValueR(instr), ConstantValue{(v1->as_int() > v2->as_int())
                                                    ? ~static_cast<i128>(0)
                                                    : 0,
                                                instr->get_type()});
      break;
    case ICmpInstrSubType::SLT:
      st.set_value(ValueR(instr), ConstantValue{(v1->as_int() < v2->as_int())
                                                    ? ~static_cast<i128>(0)
                                                    : 0,
                                                instr->get_type()});
      break;
    case ICmpInstrSubType::SLE:
      st.set_value(ValueR(instr), ConstantValue{(v1->as_int() <= v2->as_int())
                                                    ? ~static_cast<i128>(0)
                                                    : 0,
                                                instr->get_type()});
      break;
    case ICmpInstrSubType::ULT:
      st.set_value(ValueR(instr),
                   ConstantValue{(std::bit_cast<u128>(v1->as_int()) <
                                  std::bit_cast<u128>(v2->as_int()))
                                     ? ~static_cast<i128>(0)
                                     : 0,
                                 instr->get_type()});
      break;
    case ICmpInstrSubType::ULE:
      st.set_value(ValueR(instr),
                   ConstantValue{(std::bit_cast<u128>(v1->as_int()) <=
                                  std::bit_cast<u128>(v2->as_int()))
                                     ? ~static_cast<i128>(0)
                                     : 0,
                                 instr->get_type()});
      break;
    case ICmpInstrSubType::UGT:
      st.set_value(ValueR(instr),
                   ConstantValue{(std::bit_cast<u128>(v1->as_int()) >
                                  std::bit_cast<u128>(v2->as_int()))
                                     ? ~static_cast<i128>(0)
                                     : 0,
                                 instr->get_type()});
      break;
    case ICmpInstrSubType::UGE:
      st.set_value(ValueR(instr),
                   ConstantValue{(std::bit_cast<u128>(v1->as_int()) >=
                                  std::bit_cast<u128>(v2->as_int()))
                                     ? ~static_cast<i128>(0)
                                     : 0,
                                 instr->get_type()});
      break;
    case ICmpInstrSubType::EQ:
      st.set_value(ValueR(instr), ConstantValue{(v1->as_int() == v2->as_int())
                                                    ? ~static_cast<i128>(0)
                                                    : 0,
                                                instr->get_type()});
      break;
    case ICmpInstrSubType::NE:
      st.set_value(ValueR(instr), ConstantValue{(v1->as_int() != v2->as_int())
                                                    ? ~static_cast<i128>(0)
                                                    : 0,
                                                instr->get_type()});
      break;
    default:
      return false;
    case ICmpInstrSubType::INVALID:
      TODO("INVALID");
  }
  ip.instr_id++;
  return true;
}

bool interpret_sext(Instr instr, State &st, InstrPointer &ip) {
  auto v1 = get_constant_value(instr->args[0], st);
  if (!v1) {
    return false;
  }
  st.set_value(ValueR(instr), ConstantValue{v1->as_int(), instr->get_type()});
  ip.instr_id++;
  return true;
}

bool interpret_zext(Instr instr, State &st, InstrPointer &ip) {
  auto v1 = get_constant_value(instr->args[0], st);
  if (!v1) {
    return false;
  }
  // the source value may be stored sign extended, only its own bits count
  auto bitwidth = instr->args[0].get_type()->get_bitwidth();
  i128 mask = std::bit_cast<i128>(((static_cast<u128>(1)) << bitwidth) - 1);
  st.set_value(ValueR(instr),
               ConstantValue{v1->as_int() & mask, instr->get_type()});
  ip.instr_id++;
  return true;
}

bool interpret_itrunc(Instr instr, State &st, InstrPointer &ip) {
  auto v1 = get_constant_value(instr->args[0], st);
  if (!v1) {
    return false;
  }
  const u32 bitwidth = instr->get_type()->get_bitwidth();
  i128 res = v1->as_int();
  if (bitwidth < 128) {
    const u128 mask = (static_cast<u128>(1) << bitwidth) - 1;
    u128 u = static_cast<u128>(res) & mask;
    if (((u >> (bitwidth - 1)) & 1) != 0) {
      u |= ~mask;
    }
    res = static_cast<i128>(u);
  }
  st.set_value(ValueR(instr), ConstantValue{res, instr->get_type()});
  ip.instr_id++;
  return true;
}

}  // namespace

void Interpreter::dump_state() {
  fmt::println("========STATE=========");
  for (auto [v, s] : state.vals) {
    fmt::println("{} : {}", v, s);
  }
  fmt::println("======STATEEND=======");
}

bool Interpreter::step_instr() {
  auto curr_bb = ip.func->basic_blocks[ip.bb_id];
  auto curr_i = curr_bb->instructions[ip.instr_id];
  // fmt::println("{}", curr_i);
  // dump_state();

  switch (curr_i->instr_type) {
    case InstrType::BranchInstr:
      return interpret_branch(curr_i, state, ip);
    case InstrType::BinaryInstr:
      return interpret_binary_expr(curr_i, state, ip);
    case InstrType::ICmp:
      return interpret_icmp(curr_i, state, ip);
    case InstrType::CondBranchInstr:
      return interpret_cbranch(curr_i, state, ip);
    case InstrType::SExt:
      return interpret_sext(curr_i, state, ip);
    case InstrType::ZExt:
      return interpret_zext(curr_i, state, ip);
    case InstrType::ITrunc:
      return interpret_itrunc(curr_i, state, ip);
    default:
  }
  fmt::println("{}", curr_i);
  TODO("impl interpreter instr");
}

bool Interpreter::step_till_end_of_bb() {
  auto curr_bb = ip.func->basic_blocks[ip.bb_id];
  while (true) {
    auto curr = curr_bb->instructions[ip.instr_id];
    // TODO if supporting calls this is not enough depending on the semantics we
    // want for that
    bool last_was_terminator = curr->is(InstrType::BranchInstr) ||
                               curr->is(InstrType::CondBranchInstr) ||
                               curr->is(InstrType::SwitchInstr) ||
                               curr->is(InstrType::ReturnInstr);
    if (!step_instr()) {
      fmt::println("FAILED {}", curr);
      return false;
    }
    if (last_was_terminator) {
      break;
    }
  }
  return true;
}

InstrPointer Interpreter::get_ip() { return ip; }
std::optional<ConstantValue> Interpreter::get_value(fir::ValueR v) {
  if (state.vals.contains(v)) {
    return state.vals.at(v);
  }
  return {};
}

// bool Interpreter::interpret(Function *func) {}
void Interpreter::set(ValueR r, ConstantValue cv) {
  state.vals.insert({r, cv});
}

}  // namespace foptim::fir::intepreter
