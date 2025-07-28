#include "reg_alloc_wp.hpp"

#include <algorithm>

#include "mir/analysis/live_variables.hpp"
#include "mir/instr.hpp"
#include "utils/set.hpp"

namespace foptim::fmir {

struct InterferenceData {
  bool is_vec;
  TVec<size_t> collides;
};

struct ColorData {
  size_t vir_id;
  bool is_vec;
};

void replace_vargs(MInstr &instr, const TMap<size_t, ColorData> &reg_mapping);

void replace_vargs(IRVec<MBB> &bbs,
                   const TMap<size_t, ColorData> &reg_mapping) {
  for (auto &bb : bbs) {
    for (auto &instr : bb.instrs) {
      replace_vargs(instr, reg_mapping);
    }
  }
}

void replace_vargs(MInstr &instr, const TMap<size_t, ColorData> &reg_mapping) {
  for (u32 i = 0; i < instr.n_args; i++) {
    switch (instr.args[i].type) {
      case MArgument::ArgumentType::Imm:
      case MArgument::ArgumentType::Label:
      case MArgument::ArgumentType::MemLabel:
      case MArgument::ArgumentType::MemImmLabel:
      case MArgument::ArgumentType::MemImm:
        break;
      case MArgument::ArgumentType::VReg:
      case MArgument::ArgumentType::MemImmVReg:
      case MArgument::ArgumentType::MemVReg: {
        auto reg = instr.args[i].reg;
        if (!reg.is_concrete() && reg_mapping.contains(reg.virt_id())) {
          instr.args[i].reg.virt.id = reg_mapping.at(reg.virt_id()).vir_id;
        }
        break;
      }
      case MArgument::ArgumentType::MemVRegVRegScale:
      case MArgument::ArgumentType::MemImmVRegVReg:
      case MArgument::ArgumentType::MemVRegVReg:
      case MArgument::ArgumentType::MemImmVRegVRegScale: {
        auto reg = instr.args[i].reg;
        auto indx = instr.args[i].indx;
        if (!reg.is_concrete() && reg_mapping.contains(reg.virt_id())) {
          instr.args[i].reg.virt.id = reg_mapping.at(reg.virt_id()).vir_id;
        }
        if (!indx.is_concrete() && reg_mapping.contains(indx.virt_id())) {
          instr.args[i].indx.virt.id = reg_mapping.at(indx.virt_id()).vir_id;
        }
        break;
      }
      case MArgument::ArgumentType::MemImmVRegScale: {
        auto indx = instr.args[i].indx;
        if (!indx.is_concrete() && reg_mapping.contains(indx.virt_id())) {
          instr.args[i].indx.virt.id = reg_mapping.at(indx.virt_id()).vir_id;
        }
        break;
      }
    }
  }
}

static void applyf(MFunc &func) {
  TMap<size_t, InterferenceData> interference_graph;

  const auto lifetimes = linear_lifetime(func);
  size_t biggest = 0;
  bool biggest_is_vec = false;
  size_t biggest_size = 0;

  for (const auto &[reg, lifetime] : lifetimes) {
    if (reg.is_concrete()) {
      continue;
    }
    auto reg_id = reg_to_uid(reg);
    auto &g = interference_graph[reg_id];
    for (const auto &[oreg, olifetime] : lifetimes) {
      if (lifetime.collide(olifetime)) {
        g.collides.push_back(reg_to_uid(oreg));
      }
    }
    g.is_vec = reg.is_vec_reg();
    if (g.collides.size() >= biggest_size) {
      biggest_is_vec = g.is_vec;
      biggest_size = g.collides.size();
      biggest = reg_id;
    }
  }

  TMap<size_t, ColorData> reg_mapping;
  auto curr_max_vreg_id = max_vreg_id(func);
  while (!interference_graph.empty()) {
    auto curr_color = ColorData{curr_max_vreg_id++, biggest_is_vec};
    reg_mapping[biggest] = curr_color;
    // fmt::println("Biggest {} with new color {}, size: {}", biggest,
    //              curr_color.vir_id, interference_graph.size());
    ASSERT(interference_graph.contains(biggest));

    TVec<std::pair<size_t, size_t>> v_prime;
    const auto b = interference_graph[biggest].collides.begin();
    const auto e = interference_graph[biggest].collides.end();
    for (const auto &[reg, lifetime] : lifetimes) {
      auto reg_id = reg_to_uid(reg);
      if (interference_graph.contains(reg_id) &&
          reg.is_vec_reg() == curr_color.is_vec &&
          std::find(b, e, reg_id) != e && reg_id != biggest) {
        v_prime.emplace_back(reg_id,
                             interference_graph[reg_id].collides.size());
      }
    }

    std::sort(v_prime.begin(), v_prime.end(),
              [](auto a1, auto a2) { return a1.second < a2.second; });

    while (!v_prime.empty()) {
      auto [r_id, _] = v_prime.back();
      v_prime.pop_back();
      reg_mapping[r_id] = curr_color;

      for (auto e : interference_graph[r_id].collides) {
        for (size_t v_primep1 = v_prime.size(); v_primep1 > 0; v_primep1--) {
          if (v_prime[v_primep1].first == e) {
            v_prime.erase(v_prime.begin() + v_primep1);
            break;
          }
        }
      }
      interference_graph.erase(r_id);
    }
    ASSERT(interference_graph.contains(biggest));
    interference_graph.erase(biggest);

    // update collides size optional but maybe better?
    {
      for (auto &[_, inter] : interference_graph) {
        for (size_t ip1 = inter.collides.size(); ip1 > 0; ip1--) {
          bool found = false;
          for (auto &[b, _] : interference_graph) {
            if (b == inter.collides[ip1 - 1]) {
              found = true;
              break;
            }
          }
          if (!found) {
            inter.collides.erase(inter.collides.begin() + (ip1 - 1));
          }
        }
      }
    }

    biggest = 0;
    biggest_is_vec = false;
    biggest_size = 0;
    for (auto [id, inter] : interference_graph) {
      if (inter.collides.size() >= biggest_size) {
        if (inter.is_vec) {
          biggest_is_vec = true;
        }
        biggest_size = inter.collides.size();
        biggest = id;
      }
    }
  }

  TSet<size_t> new_v_reg;
  TSet<size_t> new_reg;
  for (auto [a, b] : reg_mapping) {
    if (b.is_vec) {
      new_v_reg.insert(b.vir_id);
    } else {
      new_reg.insert(b.vir_id);
    }
    // fmt::println("{}: {}", uid_to_reg(a), b.vir_id);
  }
  fmt::println("{} NRegs {} N Vec Regs", new_reg.size(), new_v_reg.size());
  replace_vargs(func.bbs, reg_mapping);

  // fmt::println("======");
}

void RegAllocWP::apply(FVec<MFunc> &funcs) {
  for (auto &func : funcs) {
    applyf(func);
  }
}

}  // namespace foptim::fmir
