#pragma once
#include <algorithm>
#include <cstddef>
#include <limits>

#include "../function_pass.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "optim/analysis/analysis_manager.hpp"

namespace foptim::optim {
class SORA final : public FunctionPass {
  struct UseRes {
    u64 offset;
    u64 access_size;  // 0 means infinite
    fir::Use user;
  };

  TVec<UseRes> handle_use(const fir::Use &use) {
    switch (use.user->instr_type) {
      case fir::InstrType::LoadInstr:
      case fir::InstrType::StoreInstr:
        // the pointer being the stored value escapes it
        if (use.argId != 0) {
          break;
        }
        return {UseRes{.offset = 0,
                       .access_size = use.user->result_type()->get_size(),
                       .user = use}};
      case fir::InstrType::BinaryInstr:
        switch (static_cast<fir::BinaryInstrSubType>(use.user->subtype)) {
          case fir::BinaryInstrSubType::PtrAdd:
            if (use.argId == 0 && use.user->args[1].is_constant() &&
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

  // Splits wide integer loads/stores of a non escaping alloca into the pieces
  // that other accesses of the alloca use, so the alloca can be split later.
  // Returns whether anything was split.
  bool split_wide_accesses(fir::Context &ctx, fir::Instr alloca) {
    struct Acc {
      fir::Instr instr;
      u64 off;
      u64 size;
    };
    TVec<Acc> accs;
    for (auto use : alloca->uses) {
      for (auto v : handle_use(use)) {
        if (v.access_size == 0) {
          return false;
        }
        accs.push_back({v.user.user, v.offset, v.access_size});
      }
    }
    if (accs.size() < 3) {
      return false;
    }
    bool modified = false;
    for (auto &w : accs) {
      auto wty = w.instr->result_type();
      const bool int_vec =
          wty->is_vec() &&
          wty->as_vec().type == fir::VectorType::SubType::Integer &&
          wty->as_vec().bitwidth % 8 == 0;
      if ((!wty->is_int() && !int_vec) || w.size < 2 ||
          w.size > (int_vec ? 32 : 8) || wty->get_bitwidth() % 8 != 0 ||
          w.instr->Volatile || w.instr->Atomic) {
        continue;
      }
      // cut points inside w, every overlapping access has to be inside
      TVec<u64> cuts{0, w.size};
      bool okak = true;
      bool any_inside = false;
      for (auto &o : accs) {
        if (&o == &w || o.off + o.size <= w.off || w.off + w.size <= o.off) {
          continue;
        }
        if (o.off < w.off || o.off + o.size > w.off + w.size) {
          okak = false;
          break;
        }
        if (o.size != w.size) {
          any_inside = true;
          cuts.push_back(o.off - w.off);
          cuts.push_back(o.off - w.off + o.size);
        }
      }
      if (!okak || !any_inside) {
        continue;
      }
      std::ranges::sort(cuts);
      cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
      for (size_t i = 0; i + 1 < cuts.size() && okak; i++) {
        auto len = cuts[i + 1] - cuts[i];
        okak = len == 1 || len == 2 || len == 4 || len == 8;
        // da vectors are split lane wise with one piece size
        okak = okak && (!int_vec || len == cuts[1] - cuts[0]);
      }
      if (!okak) {
        continue;
      }
      fir::Builder b{w.instr};
      auto piece_ptr = [&](u64 rel) {
        if (rel == 0) {
          return w.instr->args[0];
        }
        return b.build_binary_op(w.instr->args[0],
                                 fir::ValueR{ctx->get_constant_int(rel, 64)},
                                 fir::BinaryInstrSubType::PtrAdd);
      };
      if (int_vec) {
        const u64 len = cuts[1] - cuts[0];
        auto lane_ty = ctx->get_int_type(len * 8);
        auto view_ty = ctx->get_vec_type(fir::VectorType::SubType::Integer,
                                         len * 8, w.size / len);
        if (w.instr->is(fir::InstrType::StoreInstr)) {
          fir::ValueR vec = w.instr->args[1];
          if (wty != view_ty) {
            vec = b.build_conversion_op(vec, view_ty,
                                        fir::ConversionSubType::BitCast);
          }
          for (size_t i = 0; i + 1 < cuts.size(); i++) {
            fir::ValueR idx[1] = {
                fir::ValueR{ctx->get_constant_int(static_cast<i128>(i), 32)}};
            auto lane = b.build_extract_element(vec, idx, lane_ty);
            b.build_store(piece_ptr(cuts[i]), lane, false, false);
          }
        } else if (w.instr->is(fir::InstrType::LoadInstr)) {
          // the backend can not build vectors from scalar lanes, so only
          // copies (every user stores the loaded value) are split
          bool only_copies = true;
          for (auto u : w.instr->uses) {
            only_copies &= u.user->is(fir::InstrType::StoreInstr) &&
                           u.argId == 1 && !u.user->Volatile &&
                           !u.user->Atomic && u.user->result_type() == wty;
          }
          if (!only_copies || w.instr->uses.empty()) {
            continue;
          }
          TVec<fir::Instr> copies;
          for (auto u : w.instr->uses) {
            copies.push_back(u.user);
          }
          TVec<fir::ValueR> lanes;
          for (size_t i = 0; i + 1 < cuts.size(); i++) {
            lanes.push_back(
                b.build_load(lane_ty, piece_ptr(cuts[i]), false, false));
          }
          for (auto st : copies) {
            fir::Builder sb{st};
            for (size_t i = 0; i + 1 < cuts.size(); i++) {
              fir::ValueR dst = st->args[0];
              if (cuts[i] != 0) {
                dst = sb.build_binary_op(
                    dst, fir::ValueR{ctx->get_constant_int(cuts[i], 64)},
                    fir::BinaryInstrSubType::PtrAdd);
              }
              sb.build_store(dst, lanes[i], false, false);
            }
            st.destroy();
          }
        } else {
          continue;
        }
      } else if (w.instr->is(fir::InstrType::StoreInstr)) {
        for (size_t i = 0; i + 1 < cuts.size(); i++) {
          auto len = cuts[i + 1] - cuts[i];
          fir::ValueR v = w.instr->args[1];
          if (cuts[i] != 0) {
            v = b.build_binary_op(
                v,
                fir::ValueR{ctx->get_constant_int(static_cast<i64>(cuts[i] * 8),
                                                  wty->get_bitwidth())},
                fir::BinaryInstrSubType::Shr);
          }
          if (len != w.size) {
            v = b.build_itrunc(v, ctx->get_int_type(len * 8));
          }
          b.build_store(piece_ptr(cuts[i]), v, false, false);
        }
      } else if (w.instr->is(fir::InstrType::LoadInstr)) {
        fir::ValueR res;
        for (size_t i = 0; i + 1 < cuts.size(); i++) {
          auto len = cuts[i + 1] - cuts[i];
          fir::ValueR v = b.build_load(ctx->get_int_type(len * 8),
                                       piece_ptr(cuts[i]), false, false);
          if (len != w.size) {
            v = b.build_zext(v, wty);
          }
          if (cuts[i] != 0) {
            v = b.build_binary_op(
                v,
                fir::ValueR{ctx->get_constant_int(static_cast<i64>(cuts[i] * 8),
                                                  wty->get_bitwidth())},
                fir::BinaryInstrSubType::Shl);
          }
          res = i == 0 ? v
                       : b.build_binary_op(res, v, fir::BinaryInstrSubType::Or);
        }
        w.instr->replace_all_uses(res);
      } else {
        continue;
      }
      w.instr.destroy();
      // the use lists changed, the caller redoes the analysis
      return true;
    }
    return modified;
  }

  // returns whether the alloca got split
  bool apply(fir::Context &ctx, fir::Instr alloca) {
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
      return false;
    }
    std::erase_if(ress, [cutoff](const auto &x) {
      return (x.first + x.second.size) > cutoff || x.second.size == 0;
    });
    if (ress.size() <= 1) {
      return false;
    }

    // fmt::println("For alloca {} cutoff {}", alloca, cutoff);
    for (auto &[off, data] : ress) {
      fir::Builder bb{alloca};
      // fmt::println("  @{} Size:{}", off, data.size);
      auto new_alloca =
          bb.build_alloca(fir::ValueR{ctx->get_constant_int(data.size, 32)});
      for (auto u : data.uses) {
        u.replace_use(new_alloca);
        // fmt::println("    USE: {}", u.user);
      }
    }
    return true;
  }

 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedNC("SORA", COLOR_OPTIMF);
    auto entry = func.get_entry();
    // if (func.name != "_Z9WikiMergeP4Test5RangeS1_S1_PFbS_S_ES0_l") {
    //   return;
    // }
    bool modified = false;
    // splitting inserts and removes instructions of the entry block
    TVec<fir::Instr> allocas;
    for (auto instr : entry->instructions) {
      if (instr->is(fir::InstrType::AllocaInstr)) {
        allocas.push_back(instr);
      }
    }
    for (auto instr : allocas) {
      while (split_wide_accesses(ctx, instr)) {
        modified = true;
      }
      modified |= apply(ctx, instr);
    }
    return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
  }
};

}  // namespace foptim::optim
