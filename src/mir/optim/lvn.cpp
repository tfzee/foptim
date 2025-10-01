#include <fmt/core.h>

#include "lvn.hpp"
#include "mir/instr.hpp"

namespace foptim::fmir {

namespace {
bool is_applicable(MInstr &instr) {
  switch (instr.bop) {
    case GOpcode::X86:
      switch ((X86Subtype)instr.sop) {
        case X86Subtype::lea:
          return true;
        default:
          break;
      }
      break;
    case GOpcode::GBase:
    case GOpcode::GJmp:
    case GOpcode::GConv:
    case GOpcode::GArith:
    case GOpcode::GCMov:
    case GOpcode::GVec:
      break;
  }
  return false;
}
}  // namespace

bool LVN::equal_enough(const MInstr &in1, const MInstr &in2) {
  return in1.bop == in2.bop && in1.sop == in2.sop && in1.n_args == in2.n_args;
}

MArgument LVN::get_reg(Type type) {
  // auto size = get_size(type);
  // ASSERT(size <= 255);
  // bool is_float = type == Type::Float32 || type == Type::Float64;
  return {VReg{unique_reg_id++, type}, type};
}

void LVN::apply_impl(MBB &bb) {
  for (size_t i1 = 0; i1 < bb.instrs.size(); i1++) {
    if (!is_applicable(bb.instrs[i1])) {
      continue;
    }
    // TODO: just doing +10 to minimize the lifetime extension this causes
    // prob should try some different values (or a better solution)
    for (size_t i2 = i1 + 1; i2 < std::min(i1 + 10, bb.instrs.size()); i2++) {
      auto &in1 = bb.instrs[i1];
      auto &in2 = bb.instrs[i2];
      if (equal_enough(in1, in2)) {
        // fmt::println("{} {}", bb.instrs[i1], bb.instrs[i2]);
        if (in1.bop == GOpcode::X86 && (X86Subtype)in1.sop == X86Subtype::lea) {
          if (in1.args[1].type == MArgument::ArgumentType::MemLabel &&
              in1.args[1] == in2.args[1]) {
            auto new_reg = get_reg(in1.args[0].ty);
            MInstr copy_i1 = bb.instrs[i1];
            copy_i1.args[0] = new_reg;
            bb.instrs[i1] =
                MInstr{GBaseSubtype::mov, bb.instrs[i1].args[0], new_reg};
            bb.instrs[i2] =
                MInstr{GBaseSubtype::mov, bb.instrs[i2].args[0], new_reg};
            bb.instrs.insert(bb.instrs.begin() + i1, copy_i1);
            i1--;
            break;
          }
        }
      }
    }
  }
}

void LVN::apply(MFunc &func) {
  unique_reg_id = 0;
  for (auto &bb : func.bbs) {
    for (auto &instr : bb.instrs) {
      for (u8 i = 0; i < instr.n_args; i++) {
        switch (instr.args[i].type) {
          case MArgument::ArgumentType::VReg:
          case MArgument::ArgumentType::MemVReg:
          case MArgument::ArgumentType::MemImmVReg:
            if (!instr.args[i].reg.is_concrete()) {
              unique_reg_id =
                  std::max(unique_reg_id, instr.args[i].reg.virt_id());
            }
            break;
          case MArgument::ArgumentType::MemImmVRegScale:
            if (!instr.args[i].indx.is_concrete()) {
              unique_reg_id =
                  std::max(unique_reg_id, instr.args[i].indx.virt_id());
            }
            break;
          case MArgument::ArgumentType::MemVRegVReg:
          case MArgument::ArgumentType::MemImmVRegVReg:
          case MArgument::ArgumentType::MemVRegVRegScale:
          case MArgument::ArgumentType::MemImmVRegVRegScale:
            if (!instr.args[i].reg.is_concrete()) {
              unique_reg_id =
                  std::max(unique_reg_id, instr.args[i].reg.virt_id());
            }
            if (!instr.args[i].indx.is_concrete()) {
              unique_reg_id =
                  std::max(unique_reg_id, instr.args[i].indx.virt_id());
            }
            break;
          default:
            break;
        }
      }
    }
  }
  unique_reg_id++;

  for (auto &bb : func.bbs) {
    apply_impl(bb);
  }
}
}  // namespace foptim::fmir
