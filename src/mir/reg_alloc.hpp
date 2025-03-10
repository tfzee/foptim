#pragma once
#include "ir/IRLocation.hpp"
#include "ir/value.hpp"
#include "mir/instr.hpp"
#include "utils/map.hpp"
#include "utils/set.hpp"

namespace foptim::fir {
struct IRLocation;
}
namespace foptim::optim {
class LiveVariables;
} // namespace foptim::optim
namespace foptim::fmir {

class FunctionRegAlloatorParent {
  // Register get_register(ValueR value);
};

class DumbRegAlloc : public FunctionRegAlloatorParent {
  u64 vreg_num = 1;
  TSet<VReg> free_regs;
  TMap<fir::ValueR, VReg> mapping;

public:
  VReg get_new_register(Type type);
  void reset();

  DumbRegAlloc();
  // void alloc_func(fir::Function &, optim::LiveVariables &lives);
  void dump();
  VReg get_register(fir::ValueR valu);
};

} // namespace foptim::fmir
