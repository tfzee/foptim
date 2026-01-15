#pragma once
#include <limits>

#include "../function_pass.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"

namespace foptim::optim {
class SORA final : public FunctionPass {
  struct UseRes {
    u64 offset;
    u64 access_size;  // 0 means infinite
    fir::Use user;
  };

  TVec<UseRes> handle_use(const fir::Use& use) {
    switch (use.user->instr_type) {
      case fir::InstrType::LoadInstr:
      case fir::InstrType::StoreInstr:
        return {UseRes{.offset = 0,
                       .access_size = use.user->get_type()->get_size(),
                       .user = use}};
      case fir::InstrType::BinaryInstr:
        switch ((fir::BinaryInstrSubType)use.user->subtype) {
          case fir::BinaryInstrSubType::IntAdd:
            if (use.user->args[1].is_constant() &&
                use.user->args[1].as_constant()->is_int()) {
              TVec<UseRes> ress;
              auto myoff = use.user->args[1].as_constant()->as_int();
              for (auto sub_use : use.user->uses) {
                auto v = handle_use(sub_use);
                for (auto sv : v) {
                  sv.offset += myoff;
                  ress.push_back(sv);
                }
              }
              return ress;
            }
          default:
        }
        break;
      default:
      case fir::InstrType::CallInstr:
        // fmt::println("Failed with user {}", use.user);
        break;
    }
    return {UseRes{.offset = 0, .access_size = 0, .user = use}};
  }

  void apply(fir::Context& ctx, fir::Instr alloca) {
    struct Data {
      u64 size;
      TVec<fir::Use> uses;
    };
    TOMap<u64, Data> ress;

    // everything behind cutoff is dead
    u64 cutoff = std::numeric_limits<u64>::max();
    // fmt::println("===================\n{:cd}", alloca);
    for (auto use : alloca->uses) {
      for (auto v : handle_use(use)) {
        // fmt::println("{:cd} {}@{}s", v.user, v.offset, v.access_size);
        if (v.offset > cutoff) {
          continue;
        }
        if (v.access_size == 0) {
          cutoff = v.offset;
          continue;
        }
        auto lb = ress.lower_bound(v.offset);

        // need to check if the previous one doesnt overlap into this one
        if (lb != std::begin(ress)) {
          auto prev = std::prev(lb);
          if (prev->first + prev->second.size > v.offset) {
            cutoff = prev->first;
            prev->second.size = 0;
            prev->second.uses.clear();
            continue;
          }
        }
        // if we dont overlap with previous + were at the end just insert
        if (lb == ress.end()) {
          ress.insert({v.offset, {.size = v.access_size, .uses = {v.user}}});
          continue;
        }
        // if we have same offset as lb && same size we can just continue
        if (lb->first == v.offset && lb->second.size == v.access_size) {
          lb->second.uses.push_back(v.user);
          continue;
        }
        // if we dont have a matching we need to check that we dont overlap the
        // next one
        if (lb->first > v.offset) {
          if (v.offset + v.access_size <= lb->first) {
            ress.insert({v.offset, {.size = v.access_size, .uses = {v.user}}});
          } else {
            cutoff = v.offset;
            ress.insert({v.offset, {.size = 0, .uses = {}}});
          }
          continue;
        }
        // then the only option is that they match but the size is different
        ASSERT(lb->first == v.offset)
        lb->second.size = std::max(lb->second.size, v.access_size);
        lb->second.uses.push_back(v.user);
        // need then to recheck that we dont overlap to the next
        if (lb->first + lb->second.size > std::next(lb)->first) {
          cutoff = v.offset;
          lb->second.size = 0;
          lb->second.uses.clear();
        }
      }
    }
    if (cutoff == 0 || ress.size() <= 1) {
      return;
    }
    std::erase_if(ress, [cutoff](const auto& x) {
      return (x.first + x.second.size) > cutoff || x.second.size == 0;
    });
    if (ress.size() <= 1) {
      return;
    }

    // fmt::println("For alloca {} cutoff {}", alloca, cutoff);
    for (auto& [off, data] : ress) {
      fir::Builder bb{alloca};
      // fmt::println("  @{} Size:{}", off, data.size);
      auto new_alloca =
          bb.build_alloca(fir::ValueR{ctx->get_constant_int(data.size, 32)});
      for (auto u : data.uses) {
        u.replace_use(new_alloca);
        // fmt::println("    USE: {}", u.user);
      }
    }
  }

 public:
  void apply(fir::Context& ctx, fir::Function& func) override {
    ZoneScopedNC("SORA", COLOR_OPTIMF);
    auto entry = func.get_entry();
    // if (func.name != "_Z9WikiMergeP4Test5RangeS1_S1_PFbS_S_ES0_l") {
    //   return;
    // }
    for (auto instr : entry->instructions) {
      if (instr->is(fir::InstrType::AllocaInstr)) {
        apply(ctx, instr);
      }
    }
  }
};

}  // namespace foptim::optim
