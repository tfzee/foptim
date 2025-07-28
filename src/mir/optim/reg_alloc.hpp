#pragma once
#include "../func.hpp"

namespace foptim::fmir {

// replace virtual arguments by a vregtype
void replace_vargs(MInstr &instr, const TMap<u64, CReg> &reg_mapping);
void replace_vargs(IRVec<MBB> &bbs, const TMap<u64, CReg> &reg_mapping);

class RegAlloc {
 public:
  void apply(FVec<MFunc> &funcs);
};

}  // namespace foptim::fmir
