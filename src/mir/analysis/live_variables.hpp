#include "mir/analysis/cfg.hpp"
#include "mir/func.hpp"
#include "utils/bitset.hpp"

namespace foptim::fmir {

class LiveVariables {
public:
  const CFG &cfg;
  IRVec<utils::BitSet<>> _live;
  
  LiveVariables(const CFG &cfg, fmir::MFunc &func) : cfg(cfg) { update(func); }
  bool isAlive(const VReg &reg, size_t bb_id);
  void update(fmir::MFunc &func);
};

} // namespace foptim::fmir
