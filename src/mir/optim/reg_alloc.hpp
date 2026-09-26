#pragma once
#include "../func.hpp"
#include "mir/optim/function_pass.hpp"

namespace foptim::fmir {

// replace virtual arguments by a vregtype
void replace_vargs(MInstr &instr, const TMap<u64, CReg> &reg_mapping);
void replace_vargs(IRVec<MBB> &bbs, const TMap<u64, CReg> &reg_mapping);
void replace_varg(MInstr &instr, u64 from, VReg to, bool keep_type);

class RegAlloc2 : public FunctionPass {
public:
  void apply(MFunc &func, const conf::CompConf &conf) final;
};

} // namespace foptim::fmir
