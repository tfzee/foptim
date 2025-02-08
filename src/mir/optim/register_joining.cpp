#include "register_joining.hpp"
#include "mir/analysis/live_variables.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "mir/optim/reg_alloc.hpp"

namespace foptim::fmir {

void RegisterJoining::apply(MFunc &func) {
  auto lives = linear_lifetime(func);
  TMap<size_t, VRegType> reg_mapping;

  for (size_t ip1 = func.bbs.size(); ip1 > 0; ip1--) {
    size_t i = ip1 - 1;
    auto &bb = func.bbs[i];

    for (size_t jp1 = bb.instrs.size(); jp1 > 0; jp1--) {
      size_t j = jp1 - 1;
      auto &instr = bb.instrs[j];
      bool hit = false;

      VReg pinned_target_reg;
      VReg virtual_value_reg;

      if (instr.op == Opcode::mov && instr.args[0].isReg() &&
          instr.args[1].isReg() && instr.args[0].reg.info.is_pinned() &&
          !instr.args[1].reg.info.is_pinned()) {
        pinned_target_reg = instr.args[0].reg;
        virtual_value_reg = instr.args[1].reg;
        hit = true;
      } else if (instr.op == Opcode::mov && instr.args[0].isReg() &&
                 instr.args[1].isReg() && instr.args[1].reg.info.is_pinned() &&
                 !instr.args[0].reg.info.is_pinned()) {
        pinned_target_reg = instr.args[1].reg;
        virtual_value_reg = instr.args[0].reg;
        hit = true;
      } else if (instr.op == Opcode::arg_setup && instr.args[0].isReg() &&
                 instr.args[1].isReg() && !instr.args[0].reg.info.is_pinned() &&
                 instr.args[1].reg.info.is_pinned()) {
        virtual_value_reg = instr.args[0].reg;
        pinned_target_reg = instr.args[1].reg;
        hit = true;
      }
      if (pinned_target_reg.info.ty == VRegType::SP ||
          pinned_target_reg.info.ty == VRegType::BP ||
          pinned_target_reg.info.reg_class !=
              virtual_value_reg.info.reg_class) {
        hit = false;
      }

      if (hit) {
        ASSERT(lives.contains(virtual_value_reg));
        ASSERT(lives.contains(pinned_target_reg));

        if (!lives.at(pinned_target_reg).collide(lives.at(virtual_value_reg))) {
          reg_mapping[virtual_value_reg.id] = pinned_target_reg.info.ty;
          lives.at(pinned_target_reg).update(lives.at(virtual_value_reg));
        }
      }
    }
  }
  replace_vargs(func.bbs, reg_mapping);
}

void RegisterJoining::apply(foptim::FVec<MFunc> &funcs) {
  for (auto &f : funcs) {
    apply(f);
  }
}

} // namespace foptim::fmir
