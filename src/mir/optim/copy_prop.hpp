#pragma once
#include "../func.hpp"
#include "mir/instr.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class LocalCopyPropagation {
  void apply(MBB &bb) {
    TMap<VReg, VReg> mappings;
    TVec<MArgument> temp;

    for (auto &instr : bb.instrs) {
      switch (instr.op) {
      case Opcode::mov:
        if (instr.args[0].isReg() && instr.args[1].isReg() &&
            instr.args[0] != instr.args[1]) {
          mappings[instr.args[0].reg] = instr.args[1].reg;
        }
        break;
      case Opcode::call:
      case Opcode::invoke:
        mappings.clear();
        break;
      default: {
        temp.clear();
        written_args(instr, temp);
        for (auto written : temp) {
          switch (written.type) {
          case MArgument::ArgumentType::Imm:
          case MArgument::ArgumentType::MemImm:
          case MArgument::ArgumentType::Label:
          case MArgument::ArgumentType::MemLabel:
          case MArgument::ArgumentType::MemImmLabel:
            break;
          case MArgument::ArgumentType::VReg:
          case MArgument::ArgumentType::MemVReg:
          case MArgument::ArgumentType::MemImmVReg:
            mappings.erase(written.reg);
            break;
          case MArgument::ArgumentType::MemVRegVReg:
          case MArgument::ArgumentType::MemImmVRegVReg:
          case MArgument::ArgumentType::MemVRegVRegScale:
          case MArgument::ArgumentType::MemImmVRegScale:
          case MArgument::ArgumentType::MemImmVRegVRegScale:
            mappings.erase(written.reg);
            mappings.erase(written.indx);
            break;
          }
        }
        temp.clear();
        read_args(instr, temp);
        for (auto written : temp) {
          switch (written.type) {
          case MArgument::ArgumentType::Imm:
          case MArgument::ArgumentType::MemImm:
          case MArgument::ArgumentType::Label:
          case MArgument::ArgumentType::MemLabel:
          case MArgument::ArgumentType::MemImmLabel:
            break;
          case MArgument::ArgumentType::VReg:
          case MArgument::ArgumentType::MemVReg:
          case MArgument::ArgumentType::MemImmVReg:
            if (mappings.contains(written.reg)) {
              fmt::println("Mathc1! {}", instr);
            }
            break;
          case MArgument::ArgumentType::MemVRegVReg:
          case MArgument::ArgumentType::MemImmVRegVReg:
          case MArgument::ArgumentType::MemVRegVRegScale:
          case MArgument::ArgumentType::MemImmVRegScale:
          case MArgument::ArgumentType::MemImmVRegVRegScale:
            if (mappings.contains(written.reg)) {
              fmt::println("Mathc2! {}", instr);
            }
            if (mappings.contains(written.indx)) {
              fmt::println("Mathc3! {}", instr);
            }
            break;
          }
        }

        break;
      }
      }
    }
  }

public:
  void apply(FVec<MFunc> &funcs) {
    for (auto &f : funcs) {
      for (auto &bb : f.bbs) {
        apply(bb);
      }
    }
  }
};

} // namespace foptim::fmir
