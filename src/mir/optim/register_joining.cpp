#include "mir/analysis/live_variables.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "mir/optim/reg_alloc.hpp"
#include "register_joining.hpp"

namespace foptim::fmir {

void RegisterJoining::apply(MFunc &func) {
  auto colls = reg_coll(func);
  TMap<u64, CReg> reg_mapping;

  for (size_t ip1 = func.bbs.size(); ip1 > 0; ip1--) {
    size_t i = ip1 - 1;
    auto &bb = func.bbs[i];

    for (size_t jp1 = bb.instrs.size(); jp1 > 0; jp1--) {
      size_t j = jp1 - 1;
      auto &instr = bb.instrs[j];
      bool hit = false;

      VReg pinned_target_reg;
      VReg virtual_value_reg;

      if (instr.is(GBaseSubtype::mov) && instr.args[0].isReg() &&
          instr.args[1].isReg() && instr.args[0].reg.is_concrete() &&
          !instr.args[1].reg.is_concrete()) {
        pinned_target_reg = instr.args[0].reg;
        virtual_value_reg = instr.args[1].reg;
        hit = true;
      } else if (instr.is(GBaseSubtype::mov) && instr.args[0].isReg() &&
                 instr.args[1].isReg() && instr.args[1].reg.is_concrete() &&
                 !instr.args[0].reg.is_concrete()) {
        pinned_target_reg = instr.args[1].reg;
        virtual_value_reg = instr.args[0].reg;
        hit = true;
      } else if (instr.is(GBaseSubtype::arg_setup) && instr.args[0].isReg() &&
                 instr.args[1].isReg() && !instr.args[0].reg.is_concrete() &&
                 instr.args[1].reg.is_concrete()) {
        virtual_value_reg = instr.args[0].reg;
        pinned_target_reg = instr.args[1].reg;
        hit = true;
      }
      if (hit &&
          (pinned_target_reg.c_reg() == CReg::SP ||
           pinned_target_reg.c_reg() == CReg::BP ||
           pinned_target_reg.is_vec_reg() != virtual_value_reg.is_vec_reg())) {
        hit = false;
      }

      if (hit) {
        ASSERT(colls.contains(virtual_value_reg));
        ASSERT(colls.contains(pinned_target_reg));
        // fmt::println("TRYING JOIN {}", virtual_value_reg);
        // lives.at(virtual_value_reg).dump();
        // fmt::println("TRYING JOIN {}", pinned_target_reg);
        // lives.at(pinned_target_reg).dump();

        if (!colls.at(pinned_target_reg)
                 .contains(reg_to_uid(virtual_value_reg))) {
          // fmt::println(" WORKED");
          reg_mapping[virtual_value_reg.virt_id()] = pinned_target_reg.c_reg();
          const auto &other_colls = colls.at(virtual_value_reg);
          colls.at(pinned_target_reg)
              .insert(other_colls.begin(), other_colls.end());
        } else {
          // fmt::println(" FAILED");
        }
      }
    }
  }
  replace_vargs(func.bbs, reg_mapping);
}

void RegisterJoining::apply(foptim::FVec<MFunc> &funcs) {
  ZoneScopedN("Register Joining");
  for (auto &f : funcs) {
    apply(f);
  }
}

}  // namespace foptim::fmir
