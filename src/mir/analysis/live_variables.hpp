#include "mir/analysis/cfg.hpp"
#include "mir/func.hpp"
#include "utils/bitset.hpp"

namespace foptim::fmir {

class LiveVariables {
public:
  const CFG &cfg;
  IRVec<utils::BitSet<>> _live;

  LiveVariables(const CFG &cfg, const fmir::MFunc &func) : cfg(cfg) {
    update(func);
  }
  bool isAlive(const VReg &reg, size_t bb_id);
  void update(const fmir::MFunc &func);
};

size_t reg_to_uid(VReg r);
// VReg uid_to_reg(size_t r);
void update_def(const MInstr &instr, utils::BitSet<> &def);

} // namespace foptim::fmir
