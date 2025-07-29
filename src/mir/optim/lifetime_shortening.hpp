#pragma once
#include "../func.hpp"
#include "mir/instr.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

class LifetimeShortening {
  void apply(MBB &bb) {
    TMap<VReg, VReg> mappings;
    TVec<ArgData> temp;

    for (auto &instr : bb.instrs) {
      if (instr.bop == GOpcode::GBase) {
        switch ((GBaseSubtype)instr.sop) {
          case GBaseSubtype::mov:
            if (instr.args[0].isReg() && instr.args[1].isReg() &&
                instr.args[0] != instr.args[1] &&
                instr.args[0].ty == instr.args[1].ty) {
              mappings[instr.args[1].reg] = instr.args[0].reg;
            }
            continue;
          case GBaseSubtype::call:
          case GBaseSubtype::invoke:
            mappings.clear();
            continue;
          default:
            break;
        }
      }
      {
        temp.clear();
        written_args(instr, temp);
        for (auto [_, written] : temp) {
          switch (written.type) {
            case MArgument::ArgumentType::Imm:
            case MArgument::ArgumentType::MemImm:
            case MArgument::ArgumentType::Label:
            case MArgument::ArgumentType::MemLabel:
            case MArgument::ArgumentType::MemImmLabel:
              break;
            case MArgument::ArgumentType::MemImmVRegScale:
              mappings.erase(written.indx);
              for (auto it = mappings.begin(); it != mappings.end();) {
                if (it->second == written.indx) {
                  it = mappings.erase(it);
                  break;
                }
                ++it;
              }
              break;
            case MArgument::ArgumentType::MemVRegVReg:
            case MArgument::ArgumentType::MemImmVRegVReg:
            case MArgument::ArgumentType::MemVRegVRegScale:
            case MArgument::ArgumentType::MemImmVRegVRegScale:
              mappings.erase(written.indx);
              for (auto it = mappings.begin(); it != mappings.end();) {
                if (it->second == written.indx) {
                  it = mappings.erase(it);
                  break;
                }
                ++it;
              }
              mappings.erase(written.reg);
              for (auto it = mappings.begin(); it != mappings.end();) {
                if (it->second == written.reg) {
                  it = mappings.erase(it);
                  break;
                }
                ++it;
              }
              break;
            case MArgument::ArgumentType::VReg:
            case MArgument::ArgumentType::MemVReg:
            case MArgument::ArgumentType::MemImmVReg:
              mappings.erase(written.reg);
              for (auto it = mappings.begin(); it != mappings.end();) {
                if (it->second == written.reg) {
                  it = mappings.erase(it);
                  break;
                }
                ++it;
              }
              break;
          }
        }
        temp.clear();
        read_args(instr, temp);
        for (auto [id, read] : temp) {
          switch (read.type) {
            case MArgument::ArgumentType::Imm:
            case MArgument::ArgumentType::MemImm:
            case MArgument::ArgumentType::Label:
            case MArgument::ArgumentType::MemLabel:
            case MArgument::ArgumentType::MemImmLabel:
              break;
            case MArgument::ArgumentType::VReg:
              if (mappings.contains(read.reg) &&
                  !instr.args[id].reg.is_concrete()) {
                instr.args[id] =
                    MArgument{mappings.at(read.reg), instr.args[id].ty};
              }
              break;
            case MArgument::ArgumentType::MemVReg:
            case MArgument::ArgumentType::MemImmVReg:
            case MArgument::ArgumentType::MemImmVRegScale:
            case MArgument::ArgumentType::MemVRegVReg:
            case MArgument::ArgumentType::MemImmVRegVReg:
            case MArgument::ArgumentType::MemVRegVRegScale:
            case MArgument::ArgumentType::MemImmVRegVRegScale:
              // TODO: some of these might be able to be fixed
              break;
          }
        }
      }
    }
  }

 public:
  void apply(MFunc &func) {
    for (auto &bb : func.bbs) {
      apply(bb);
    }
  }
  void apply(FVec<MFunc> &funcs) {
    for (auto &f : funcs) {
      apply(f);
    }
  }
};

}  // namespace foptim::fmir
