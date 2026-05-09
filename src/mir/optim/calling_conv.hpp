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
  enum class Req : u8 {
    // NOTE: order matters
    NotSupported = 0,
    Supported = 1,
    Required = 2,
  };
  struct Args {
    FVec<fmir::CReg> gpr = {};
    FVec<fmir::CReg> fvr = {};
    bool caller_cleanup = true;
    bool allows_stack_args = true;
    // TODO: assumes size of 8
    //  .stack_arg_min_size = 8,
    //  .stack_arg_max_size = 8,
  } args;
  struct Ret {
    SmallFVec<fmir::CReg, 4> gpr = {};
    SmallFVec<fmir::CReg, 4> fvr = {};
    u64 max_ret_regs;
    enum RetConv {
      // only gpr/fvr returns no mixing
      NoMixing,
      // can mix gpr/fvr returns but not gpr[0] and fvr[0] at the same time
      // NoOverlap,
      // can freely match and mix the above
      Mixing,
    };
    // cant have both fvr[0] and gpr[0] active at same time
    RetConv conv;
  } rets;

  // caller saved *must* contain all arguments and return regs aswell
  FVec<fmir::CReg> caller_saved = {};
  FVec<fmir::CReg> callee_saved = {};

  Req full_pro_epilogue = Req::Supported;
  // bool requires_red_zone;
  // 0 for disabled
  struct Alignment {
    Req alignment = Req::Supported;
    u32 alignment_value = 0;
  } align;

  // support link reg for smoll funcs or indirect calls
  // lea link_reg, [rip+1]
  // jmp func
  // ret link_reg
  // in my testing up to 2x for basic test
  std::optional<fmir::CReg> link_reg = {};

  struct VarArg {
    Req supported;
    Req needs_register_save_area;
    std::optional<fmir::CReg> n_gpr_regs_stor = {};
    std::optional<fmir::CReg> n_fvr_regs_stor = {};
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

            .caller_cleanup = true,
            .allows_stack_args = true,
        },
    .rets =
        {
            .gpr = {CReg::A, CReg::D},
            .fvr = {CReg::mm0, CReg::mm1},
            .max_ret_regs = 2,
            .conv = CallingConvDefinition::Ret::RetConv::NoMixing,
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
    .align = {CallingConvDefinition::Req::Required, 16},
    .link_reg = std::nullopt,
    .var_arg = {CallingConvDefinition::Req::Supported,
                CallingConvDefinition::Req::Supported,
                std::nullopt,
                {CReg::A}},
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
public:
  void apply(MFunc &func, const conf::CompConf &conf) final override {
    first_stage(func, CCallHelper, conf);
  }
};

class CallingConvSecond : public FunctionPass, CallingConvImpl {
public:
  void apply(MFunc &func, const conf::CompConf &conf) final override {
    second_stage(func, CCallHelper, conf);
  }
};

} // namespace foptim::fmir
