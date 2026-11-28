#include "lvn.hpp"

#include <fmt/core.h>

#include "mir/instr.hpp"

namespace foptim::fmir {

namespace {
bool is_applicable(MInstr &instr) {
  switch (instr.bop) {
    case GOpcode::X86:
      switch (static_cast<X86Subtype>(instr.sop)) {
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
        if (in1.bop == GOpcode::X86 &&
            static_cast<X86Subtype>(in1.sop) == X86Subtype::lea) {
          if (in1.args[1].type == MArgument::ArgumentType::MemLabel &&
              in1.args[1] == in2.args[1]) {
            auto new_reg = get_reg(in1.args[0].ty);
            MInstr copy_i1 = bb.instrs[i1];
            copy_i1.args[0] = new_reg;
            bb.instrs[i1] =
                MInstr{GBaseSubtype::mov, bb.instrs[i1].args[0], new_reg};
            bb.instrs[i2] =
                MInstr{GBaseSubtype::mov, bb.instrs[i2].args[0], new_reg};
            bb.instrs.insert(bb.instrs.begin() + static_cast<i64>(i1), copy_i1);
            i1--;
            break;
          }
        }
      }
    }
  }
}

namespace {
bool is_all_ones(const MInstr &instr) {
  return instr.is(X86Subtype::vpcmpeq) && instr.n_args == 3 &&
         instr.args[0].isReg() && !instr.args[0].reg.is_concrete() &&
         instr.args[1] == instr.args[0] && instr.args[2] == instr.args[0];
}

// An all ones vector made again later in the block becomes a copy of the first
// one. The mask of every vector not is built on its own otherwise.
void share_all_ones(MFunc &func) {
  bool any = false;
  for (const auto &bb : func.bbs) {
    for (const auto &instr : bb.instrs) {
      any |= is_all_ones(instr);
    }
  }
  if (!any) {
    return;
  }
  TMap<u64, u32> n_defs;
  TVec<ArgData> written;
  for (const auto &bb : func.bbs) {
    for (const auto &instr : bb.instrs) {
      written.clear();
      written_args(instr, written);
      for (const auto &w : written) {
        if (w.arg.isReg() && !w.arg.reg.is_concrete()) {
          n_defs[w.arg.reg.virt_id()]++;
        }
      }
    }
  }
  // the shared register stays alive until the last copy, so the distance is
  // bounded to keep it from living across a huge block
  constexpr size_t MaxShareDistance = 48;
  struct First {
    MArgument reg;
    size_t pos;
  };
  TMap<Type, First> first;
  for (auto &bb : func.bbs) {
    first.clear();
    for (size_t i = 0; i < bb.instrs.size(); i++) {
      auto &instr = bb.instrs[i];
      if (!is_all_ones(instr)) {
        continue;
      }
      const auto dst = instr.args[0];
      auto it = first.find(dst.ty);
      if (it == first.end() || i - it->second.pos > MaxShareDistance) {
        if (n_defs[dst.reg.virt_id()] == 1) {
          first.insert_or_assign(dst.ty, First{dst, i});
        }
        continue;
      }
      instr = MInstr{GBaseSubtype::mov, dst, it->second.reg};
    }
  }
}
}  // namespace

void LVN::apply(MFunc &func, const conf::CompConf & /*config*/) {
  share_all_ones(func);
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
