#pragma once
#include <fmt/core.h>

#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "utils/todo.hpp"
#include "utils/types.hpp"
namespace foptim::optim {

struct AccessResult {
  u8 IsWriten : 1;
  u8 IsRead : 1;
  u8 Escapes : 1;
  // TODO: volatile
  u8 VolatileRead : 1 = 0;
  u8 VolatileWrite : 1 = 0;
};

template <typename T>
concept PtrAnalysisInput = requires(T v) { v->get_uses(); };

/*runs only locally within a single function used in a function call is
 * considered escaped*/
template <PtrAnalysisInput T>
AccessResult ptr_access_analysis(T ptr) {
  AccessResult res{.IsWriten = 0, .IsRead = 0, .Escapes = 0};
  TVec<fir::Use> worklist;
  for (auto u : ptr->get_uses()) {
    worklist.push_back(u);
  }

  while (!worklist.empty()) {
    auto u = worklist.back();
    worklist.pop_back();
    fir::Instr i = u.user;
    if (u.type == fir::UseType::BBArg) {
      // TODO: impl
      res.Escapes = true;
      continue;
    }
    if (i->is(fir::BinaryInstrSubType::IntAdd)) {
      for (auto u : i->get_uses()) {
        worklist.push_back(u);
      }
      continue;
    }
    if (i->is(fir::InstrType::StoreInstr)) {
      // TODO: volatile
      if (u.argId == 0) {
        res.IsWriten = true;
      } else {
        res.Escapes = true;
      }
      continue;
    }
    if (i->is(fir::InstrType::LoadInstr)) {
      // TODO: volatile
      res.IsRead = true;
      continue;
    }
    if (i->is(fir::InstrType::CallInstr)) {
      res.Escapes = true;
      continue;
    }
    fmt::println("{:cd}", u);
    fmt::println("{:cd}", u.user);
    TODO("okak");
  }
  return res;
}

}  // namespace foptim::optim
