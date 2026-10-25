#include "optim/analysis/mem/alias_verify.hpp"

#include <algorithm>

#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/mem/alias_sets.hpp"

namespace foptim::optim {

namespace {
bool leaves_covered(const AliasSetTable &tab, SetId s, const IRVec<u32> &sets) {
  auto covered = [&](SetId leaf) {
    return std::ranges::any_of(sets,
                               [&](SetId x) { return tab.subset(leaf, x); });
  };
  if (!tab.is_union(s)) {
    return covered(s);
  }
  return std::ranges::all_of(tab.parts(s), covered);
}
}  // namespace

bool verify_access_meta(const fir::Function &func) {
  if (func.is_decl()) {
    return true;
  }
  const auto &tab = func.ctx->alias_sets();
  bool ok = true;
  auto fail = [&](const char *what) {
    fmt::println(stderr, "AccessMeta of '{}': {}", func.name.c_str(), what);
    ok = false;
  };
  for (const auto bb : func.basic_blocks) {
    for (const auto instr : bb->instructions) {
      if (!instr->has_access_meta) {
        continue;
      }
      if (!instr->is(fir::InstrType::LoadInstr) &&
          !instr->is(fir::InstrType::StoreInstr)) {
        fail("an instruction that is no load or store has metadata");
        continue;
      }
      // a copied instruction has the flag but no entry
      const fir::AccessMeta *m = func.get_access_meta(instr);
      if (m == nullptr) {
        continue;
      }
      if (m->empty()) {
        fail("an access has an empty entry");
        continue;
      }
      auto exist = [&](const IRVec<u32> &v) {
        return std::ranges::all_of(v, [&](u32 s) { return s < tab.size(); });
      };
      if (!exist(m->types) || !exist(m->scope_in) || !exist(m->not_in)) {
        fail("an access refers to a set that does not exist");
        continue;
      }
      for (size_t i = 0; i < m->types.size(); i++) {
        for (size_t j = i + 1; j < m->types.size(); j++) {
          if (tab.disjoint(m->types[i], m->types[j])) {
            fail("an access is in two disjoint type sets");
          }
        }
      }
      for (auto s : m->scope_in) {
        if (leaves_covered(tab, s, m->not_in)) {
          fail("an access is in a scope it does not alias");
        }
      }
    }
  }
  return ok;
}

bool verify_module_alias_sets(fir::ContextData &ctx) {
  return ctx.alias_sets().verify();
}

}  // namespace foptim::optim
