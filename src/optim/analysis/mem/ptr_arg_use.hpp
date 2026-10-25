#pragma once
#include <optional>

#include "ir/basic_block_arg.hpp"
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "utils/set.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

// The parameter of a known defined function that operand arg_id of the call
// (operand 0 is the callee) binds to, nothing if there is none. Only defined
// functions have the nocapture / readonly bits.
inline std::optional<fir::BBArgument> callee_param(fir::Instr call,
                                                   u32 arg_id) {
  if (arg_id == 0 || !call->args[0].is_constant_func()) {
    return std::nullopt;
  }
  auto callee = call->args[0].as_constant()->as_func();
  if (callee->is_decl() || callee->attribs.variadic) {
    return std::nullopt;
  }
  auto entry = callee->get_entry();
  if (arg_id - 1 >= entry->args.size()) {
    return std::nullopt;
  }
  return entry->args[arg_id - 1];
}

// Does a call neither keep nor write through the pointer it gets as operand
// arg_id?
inline bool call_arg_nocapture_readonly(fir::Instr call, u32 arg_id) {
  auto param = callee_param(call, arg_id);
  return param.has_value() && (*param)->nocapture && (*param)->readonly;
}

struct PtrArgUse {
  bool nocapture = true;
  bool readonly = true;
};

// What a function does with a pointer argument: it is nocapture if the
// address (or a pointer computed from it) is only loaded from, stored to,
// compared, passed to the memory intrinsics or to nocapture parameters, and
// readonly if nothing is written through it. Pointers loaded from the memory
// behind the argument are not followed (they are not based on it).
inline PtrArgUse analyze_ptr_arg_use(fir::BBArgument arg) {
  PtrArgUse res;
  TSet<fir::ValueR> seen;
  TVec<fir::ValueR> work;
  work.push_back(fir::ValueR{arg});
  seen.insert(work.back());
  auto derive = [&](fir::ValueR v) {
    if (seen.insert(v).second) {
      work.push_back(v);
    }
  };
  auto capture = [&]() {
    res.nocapture = false;
    res.readonly = false;
  };

  while (!work.empty() && (res.nocapture || res.readonly)) {
    auto v = work.back();
    work.pop_back();
    for (auto &use : *v.get_uses()) {
      if (use.type != fir::UseType::NormalArg) {
        capture();
        return res;
      }
      auto user = use.user;
      switch (user->instr_type) {
        case fir::InstrType::ICmp:
        case fir::InstrType::FCmp:
          break;
        case fir::InstrType::LoadInstr:
          break;
        case fir::InstrType::StoreInstr:
          if (use.argId == 0) {
            res.readonly = false;
          } else {
            capture();
            return res;
          }
          break;
        case fir::InstrType::Intrinsic:
          if (user->is(fir::IntrinsicSubType::Memcmp) && use.argId <= 1) {
            break;
          }
          if ((user->is(fir::IntrinsicSubType::Memcpy) ||
               user->is(fir::IntrinsicSubType::Memmove)) &&
              use.argId <= 1) {
            if (use.argId == 0) {
              res.readonly = false;
            }
            break;
          }
          if (user->is(fir::IntrinsicSubType::Memset) && use.argId == 0) {
            res.readonly = false;
            break;
          }
          capture();
          return res;
        case fir::InstrType::CallInstr: {
          auto param = callee_param(user, use.argId);
          if (!param.has_value() || !(*param)->nocapture) {
            capture();
            return res;
          }
          if (!(*param)->readonly) {
            res.readonly = false;
          }
          break;
        }
        case fir::InstrType::SelectInstr:
          if (use.argId == 0) {
            capture();
            return res;
          }
          derive(user.result());
          break;
        case fir::InstrType::BinaryInstr:
          if (user->is(fir::BinaryInstrSubType::PtrAdd) && use.argId == 0) {
            derive(user.result());
            break;
          }
          capture();
          return res;
        default:
          capture();
          return res;
      }
    }
  }
  return res;
}

}  // namespace foptim::optim
