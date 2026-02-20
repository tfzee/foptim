#pragma once
#include <fmt/core.h>

#include "ir/function.hpp"
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
AccessResult ptr_access_analysis(T ptr);

/*run access analyis but starting from a use*/
static void useptr_access_analysis(fir::Use u, AccessResult& res,
                                   TVec<fir::Use>* worklist = nullptr) {
  fir::Instr i = u.user;
  if (u.type == fir::UseType::BBArg) {
    // TODO: impl
    res.Escapes = true;
    return;
  }
  if (i->is(fir::BinaryInstrSubType::IntAdd)) {
    if (worklist) {
      for (auto u : i->get_uses()) {
        worklist->push_back(u);
      }
    } else {
      res = ptr_access_analysis(i);
    }
    return;
  }
  if (i->is(fir::InstrType::SelectInstr)) {
    if (u.argId == 1 || u.argId == 2) {
      if (worklist) {
        for (auto u : i->get_uses()) {
          worklist->push_back(u);
        }
      } else {
        res = ptr_access_analysis(i);
      }
      return;
    } else {
      TODO("unreach?");
    }
  }
  if (i->is(fir::InstrType::StoreInstr)) {
    // TODO: volatile
    if (u.argId == 0) {
      res.IsWriten = true;
    } else {
      res.Escapes = true;
    }
    return;
  }
  if (i->is(fir::InstrType::LoadInstr)) {
    // TODO: volatile
    res.IsRead = true;
    return;
  }
  if (i->is(fir::InstrType::CallInstr)) {
    if (i->args[0].is_constant()) {
      auto fun_ptr = i->args[0].as_constant();
      if (fun_ptr->is_func()) {
        auto fun = fun_ptr->as_func();
        if (fun.func->mem_read_none) {
          return;
        }
        if (fun.func->mem_read_only) {
          res.IsRead = true;
          return;
        }
        if (fun.func->mem_write_only) {
          res.IsWriten = true;
          res.Escapes = true;
          return;
        }
      }
    }
    res.Escapes = true;
    return;
  }
  fmt::println("{:cd}", u);
  fmt::println("{:cd}", u.user);
  TODO("okak");
}

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
    useptr_access_analysis(u, res, &worklist);
  }
  return res;
}

}  // namespace foptim::optim
