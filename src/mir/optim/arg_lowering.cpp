#include "arg_lowering.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"
#include "utils/todo.hpp"

namespace foptim::fmir {

void gen_arg_mapping(MFunc &func) {
  for (u32 arg_i = 0; arg_i < func.args.size(); arg_i++) {
    ASSERT(!func.args[arg_i].info.is_pinned());
    auto arg_ty = func.arg_tys[arg_i];
    auto instr = MInstr(Opcode::mov, MArgument{func.args[arg_i], arg_ty},
                        MArgument::Mem(VReg::RSP(), 8 * (arg_i + 2), arg_ty));
    func.bbs[0].instrs.insert(func.bbs[0].instrs.begin(), instr);
  }
}

static void function_prologue_epilogue(MFunc& func){
  gen_arg_mapping(func);
}

void ArgLowering::apply(FVec<MFunc> &funcs) {
  ZoneScopedN("ArgLower");
  for (auto &func : funcs) {
    function_prologue_epilogue(func);
  }
}

}
