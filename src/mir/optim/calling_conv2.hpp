#pragma once
#include <optional>

#include "../func.hpp"
#include "config/compiler_config.hpp"
#include "mir/instr.hpp"
#include "mir/optim/function_pass.hpp"
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

  struct VarArg {
    bool supported = false;
    bool needs_register_save_area = false;
  } var_arg;
};

const CallingConvDefinition CCallHelper = {
    .args =
        {
            .gpr =
                {
                    CReg::DI,
                    CReg::SI,
                    CReg::D,
                    CReg::C,
                    CReg::R8,
                    CReg::R9,
                },
            .fvr = {CReg::mm0, CReg::mm1, CReg::mm2, CReg::mm3, CReg::mm4,
                    CReg::mm5, CReg::mm6, CReg::mm7},
        },
    .rets =
        {
            .gpr = {CReg::A, CReg::D},
            .fvr = {CReg::mm0, CReg::mm1},
        },
    .caller_saved = {CReg::A,    CReg::C,    CReg::D,    CReg::SI,
                     CReg::DI,   CReg::R8,   CReg::R9,   CReg::R10,
                     CReg::R11,  CReg::mm0,  CReg::mm1,  CReg::mm2,
                     CReg::mm3,  CReg::mm4,  CReg::mm5,  CReg::mm6,
                     CReg::mm7,  CReg::mm8,  CReg::mm9,  CReg::mm10,
                     CReg::mm11, CReg::mm12, CReg::mm13, CReg::mm14,
                     CReg::mm15},
    .callee_saved =
        {
            CReg::R12,
            CReg::R13,
            CReg::R14,
            CReg::R15,
            CReg::B,
            CReg::SP,
            CReg::BP,
        },
    .alignment_req = 16,
    .allows_stack_arg_spilling = true,
    .link_reg = std::nullopt,
    .var_arg = {true, true},
};
class CallingConvImpl {
 public:
  // run before final  register alloc
  // sets up argument loading
  void first_stage(MFunc &func, const CallingConvDefinition &conv,
                   const conf::CompConf &);

  // run after final register alloc
  // lowers the invokes to calls and sets up their arguments
  void second_stage(MFunc &func, const CallingConvDefinition &conv,
                    const conf::CompConf &);
};

class CallingConvFirst : public FunctionPass, CallingConvImpl {
  void apply(MFunc &func, const conf::CompConf &conf) final override {
    first_stage(func, CCallHelper, conf);
  }
};

class CallingConvSecond : public FunctionPass, CallingConvImpl {
  void apply(MFunc &func, const conf::CompConf &conf) final override {
    second_stage(func, CCallHelper, conf);
  }
};

}  // namespace foptim::fmir
