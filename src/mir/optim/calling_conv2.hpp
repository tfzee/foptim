#pragma once
#include "../func.hpp"
#include "mir/instr.hpp"
#include "utils/SmallVec.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

struct CallingConvDefinition {
  struct Args {
    FVec<fmir::CReg> gpr = {};
    FVec<fmir::CReg> fvr = {};
  } args;
  struct Ret {
    SmallFVec<fmir::CReg, 4> gpr = {};
    SmallFVec<fmir::CReg, 4> fvr = {};
  } rets;
  FVec<fmir::CReg> caller_saved = {};
  FVec<fmir::CReg> callee_saved = {};

  bool requires_full_pro_epilogue = false;
  // bool requires_red_zone;
  // 0 for disabled
  u32 alignment_req = 0;
  u32 allows_stack_arg_spilling = 0;

  // support link reg for smoll funcs or indirect calls
  // lea link_reg, [rip+1]
  // jmp func
  // ret link_reg
  // in my testing up to 2x for basic test
  std::optional<fmir::CReg> link_reg = {};
};

class CallingConv2 {
 public:
  // run before final  register alloc
  // sets up argument loading
  void first_stage(MFunc &funcs);

  // run after final register alloc
  // lowers the invokes to calls and sets up their arguments
  void second_stage(MFunc &funcs);
};

}  // namespace foptim::fmir
