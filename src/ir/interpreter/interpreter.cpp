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

// null pointers are the zero integer
APInt int_of(const ConstantValue &c, u32 bits) {
  if (c.is_null()) {
    return APInt::zero(bits);
  }
  return c.as_apint().zext_or_trunc(bits);
}

ConstantValue bool_value(bool b, TypeR ty) {
  return ConstantValue{APInt(ty->get_bitwidth(), u64{b}), ty};
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
  auto target_bb_id = int_of(*input_val, 1).is_zero() ? 1 : 0;
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
  const auto ty = instr.result_type();
  const auto sub = static_cast<BinaryInstrSubType>(instr->subtype);
  switch (sub) {
    case BinaryInstrSubType::PtrAdd:
    case BinaryInstrSubType::IntAdd:
    case BinaryInstrSubType::IntMul:
    case BinaryInstrSubType::IntSub:
    case BinaryInstrSubType::And:
    case BinaryInstrSubType::Or:
    case BinaryInstrSubType::Xor: {
      if (!ty->is_int_or_ptr()) {
        return false;
      }
      const u32 bw = ty->get_bitwidth();
      const APInt a = int_of(*v1, bw);
      const APInt b = int_of(*v2, bw);
      APInt res = a;
      switch (sub) {
        case BinaryInstrSubType::PtrAdd:
        case BinaryInstrSubType::IntAdd:
          res = a + b;
          break;
        case BinaryInstrSubType::IntMul:
          res = a * b;
          break;
        case BinaryInstrSubType::IntSub:
          res = a - b;
          break;
        case BinaryInstrSubType::And:
          res = a & b;
          break;
        case BinaryInstrSubType::Or:
          res = a | b;
          break;
        case BinaryInstrSubType::Xor:
          res = a ^ b;
          break;
        default:
          return false;
      }
      st.set_value(instr.result(), ConstantValue{std::move(res), ty});
      break;
    }
    case BinaryInstrSubType::FloatAdd:
      if (instr.result_type()->as_float() == 32) {
        st.set_value(instr.result(), ConstantValue{v1->as_f32() + v2->as_f32(),
                                                   instr.result_type()});
      } else {
        st.set_value(instr.result(), ConstantValue{v1->as_f64() + v2->as_f64(),
                                                   instr.result_type()});
      }
      break;
    case BinaryInstrSubType::FloatMul:
      if (instr.result_type()->as_float() == 32) {
        st.set_value(instr.result(), ConstantValue{v1->as_f32() * v2->as_f32(),
                                                   instr.result_type()});
      } else {
        st.set_value(instr.result(), ConstantValue{v1->as_f64() * v2->as_f64(),
                                                   instr.result_type()});
      }
      break;
    case BinaryInstrSubType::Shl:
    case BinaryInstrSubType::Shr:
    case BinaryInstrSubType::AShr: {
      // out of range shifts are poison, do not evaluate them
      if (!ty->is_int()) {
        return false;
      }
      const u32 bw = ty->get_bitwidth();
      const APInt a = int_of(*v1, bw);
      const APInt amt = int_of(*v2, bw);
      if (amt.uge(APInt(bw, u64{bw}))) {
        return false;
      }
      const u32 n = static_cast<u32>(amt.to_u64());
      APInt res = sub == BinaryInstrSubType::Shl   ? a.shl(n)
                  : sub == BinaryInstrSubType::Shr ? a.lshr(n)
                                                   : a.ashr(n);
      st.set_value(instr.result(), ConstantValue{std::move(res), ty});
      break;
    }
    case BinaryInstrSubType::IntURem:
    case BinaryInstrSubType::IntSRem:
    case BinaryInstrSubType::IntSDiv:
    case BinaryInstrSubType::IntUDiv: {
      if (!ty->is_int()) {
        return false;
      }
      const u32 bw = ty->get_bitwidth();
      const APInt a = int_of(*v1, bw);
      const APInt b = int_of(*v2, bw);
      if (b.is_zero()) {
        return false;
      }
      APInt res = a;
      if (sub == BinaryInstrSubType::IntUDiv) {
        res = a.udiv(b);
      } else if (sub == BinaryInstrSubType::IntURem) {
        res = a.urem(b);
      } else {
        // min / -1 overflows
        if (a == APInt::signed_min(bw) && b.is_all_ones()) {
          return false;
        }
        res = sub == BinaryInstrSubType::IntSDiv ? a.sdiv(b) : a.srem(b);
      }
      st.set_value(instr.result(), ConstantValue{std::move(res), ty});
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
  const u32 bw = instr->args[0].get_type()->get_bitwidth();
  const APInt a = int_of(*v1, bw);
  const APInt b = int_of(*v2, bw);
  bool res = false;
  switch (static_cast<ICmpInstrSubType>(instr->subtype)) {
    case ICmpInstrSubType::SGE:
      res = a.sge(b);
      break;
    case ICmpInstrSubType::SGT:
      res = a.sgt(b);
      break;
    case ICmpInstrSubType::SLT:
      res = a.slt(b);
      break;
    case ICmpInstrSubType::SLE:
      res = a.sle(b);
      break;
    case ICmpInstrSubType::ULT:
      res = a.ult(b);
      break;
    case ICmpInstrSubType::ULE:
      res = a.ule(b);
      break;
    case ICmpInstrSubType::UGT:
      res = a.ugt(b);
      break;
    case ICmpInstrSubType::UGE:
      res = a.uge(b);
      break;
    case ICmpInstrSubType::EQ:
      res = a == b;
      break;
    case ICmpInstrSubType::NE:
      res = a != b;
      break;
    default:
      return false;
    case ICmpInstrSubType::INVALID:
      TODO("INVALID");
  }
  st.set_value(instr.result(), bool_value(res, instr->result_type()));
  ip.instr_id++;
  return true;
}

bool interpret_int_cast(Instr instr, State &st, InstrPointer &ip) {
  auto v1 = get_constant_value(instr->args[0], st);
  if (!v1) {
    return false;
  }
  const u32 from = instr->args[0].get_type()->get_bitwidth();
  const u32 to = instr->result_type()->get_bitwidth();
  const APInt a = int_of(*v1, from);
  APInt res = instr->is(InstrType::SExt)   ? a.sext(to)
              : instr->is(InstrType::ZExt) ? a.zext(to)
                                           : a.trunc(to);
  st.set_value(instr.result(),
               ConstantValue{std::move(res), instr->result_type()});
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
    case InstrType::ZExt:
    case InstrType::ITrunc:
      return interpret_int_cast(curr_i, state, ip);
    case InstrType::CallInstr:
      return interpret_call(curr_i);
    case InstrType::ReturnInstr:
      return interpret_ret(curr_i);
    default:
  }
  fmt::println("{}", curr_i);
  TODO("impl interpreter instr");
}

bool Interpreter::interpret_ret(Instr ret) {
  ret_vals.clear();
  for (const auto &arg : ret->args) {
    auto v = get_constant_value(arg, state);
    if (!v) {
      return false;
    }
    ret_vals.push_back(v.value());
  }
  returned = true;
  return true;
}

bool Interpreter::interpret_call(Instr call) {
  const auto &callee = call->args[0];
  if (!callee.is_constant_func()) {
    return false;
  }
  auto *func = callee.as_constant()->as_func().func;
  if (func->is_decl()) {
    return false;
  }
  Interpreter sub{func};
  const auto &entry = func->basic_blocks[0];
  ASSERT(entry->args.size() == call->args.size() - 1);
  for (size_t i = 0; i < entry->args.size(); i++) {
    auto v = get_constant_value(call->args[i + 1], state);
    if (!v) {
      return false;
    }
    sub.set(ValueR(entry->args[i]), v.value());
  }
  if (!sub.run()) {
    return false;
  }
  ASSERT(sub.return_values().size() == call->n_results());
  for (u32 i = 0; i < call->n_results(); i++) {
    state.set_value(call.result(i), sub.return_values()[i]);
  }
  ip.instr_id++;
  return true;
}

bool Interpreter::run() {
  while (!returned) {
    if (!step_instr()) {
      return false;
    }
  }
  return true;
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
