#include "lvn.hpp"

#include <fmt/base.h>

#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/types_ref.hpp"
#include "ir/value.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/mem/alias_query.hpp"
#include "optim/analysis/mem/basic_alias_test.hpp"
#include "optim/analysis/mem/mem_access.hpp"
#include "optim/analysis/mem/memory_ssa.hpp"

namespace foptim::optim {

namespace {
bool lvn_applicable(fir::Instr instr) {
  // reads of memory (memcmp) can see different values at two places
  return !instr->pot_reads_mem() && !instr->is(fir::InstrType::LoadInstr) &&
         !instr->is(fir::InstrType::StoreInstr) &&
         !instr->is(fir::InstrType::AllocaInstr) &&
         !instr->is(fir::InstrType::CallInstr);
}
bool gvn_applicable(fir::Instr instr) {
  return !instr->pot_reads_mem() && !instr->is(fir::InstrType::LoadInstr) &&
         !instr->is(fir::InstrType::StoreInstr) &&
         !instr->is(fir::InstrType::AllocaInstr) &&
         !instr->is(fir::InstrType::CallInstr) &&
         !instr->has_pot_sideeffects() && !instr->is_critical();
}

// who is the access to ptr whose metadata may prove accesses in between apart
bool may_alias_access(AliasAnalyis &aa, fir::Instr between, fir::ValueR ptr,
                      u32 size, fir::Instr who) {
  if (who.is_valid()) {
    return aa.alias_access(between, who) != AliasAnalyis::AAResult::NoAlias;
  }
  return aa.alias(between->args[0], ptr, between->result_type()->get_size(),
                  size) != AliasAnalyis::AAResult::NoAlias;
}

bool is_pot_store_between(fir::BasicBlock bb, fir::ValueR ptr, u32 size,
                          size_t from, size_t to, AliasAnalyis &aa,
                          fir::Instr who = fir::Instr{}) {
  for (size_t between_i = from; between_i < to; between_i++) {
    auto binstr = bb->instructions[between_i];
    if (binstr->is(fir::InstrType::StoreInstr)) {
      if (may_alias_access(aa, binstr, ptr, size, who)) {
        return true;
      }
    } else if (binstr->pot_modifies_mem()) {
      return true;
    }
  }
  return false;
}

bool is_pot_load_between(fir::BasicBlock bb, fir::ValueR ptr, u32 size,
                         size_t from, size_t to, AliasAnalyis &aa,
                         fir::Instr who = fir::Instr{}) {
  for (size_t between_i = from; between_i < to; between_i++) {
    auto binstr = bb->instructions[between_i];
    if (binstr->is(fir::InstrType::LoadInstr)) {
      if (may_alias_access(aa, binstr, ptr, size, who)) {
        return true;
      }
    } else if (binstr->pot_reads_mem()) {
      return true;
    }
  }
  return false;
}

bool is_pot_loadstore_between(fir::BasicBlock bb, fir::ValueR ptr, u32 size,
                              size_t from, size_t to, AliasAnalyis &aa,
                              fir::Instr who = fir::Instr{}) {
  for (size_t between_i = from; between_i < to; between_i++) {
    auto binstr = bb->instructions[between_i];
    if (binstr->is(fir::InstrType::StoreInstr) ||
        binstr->is(fir::InstrType::LoadInstr)) {
      if (may_alias_access(aa, binstr, ptr, size, who)) {
        return true;
      }
    } else if (binstr->pot_reads_mem() || binstr->pot_modifies_mem()) {
      return true;
    }
  }
  return false;
}

bool eql_instr_expr(fir::Instr a, fir::Instr b) {
  if (a->eql_expr(*b.get_raw_ptr()) && a->result_type() == b.result_type()) {
    return true;
  }
  if (a->is(fir::InstrType::BinaryInstr) &&
      b->is(fir::InstrType::BinaryInstr) && a->subtype == b->subtype &&
      a->is_commutative() && a->args[0].eql(b->args[1]) &&
      a->args[1].eql(b->args[0]) &&
      a->result_type()->get_bitwidth() == a->result_type()->get_bitwidth()) {
    return true;
  }

  return false;
}

bool apply_gvn(fir::Instr instr, fir::BasicBlock bb, const CFG &cfg,
               const Dominators &dom) {
  auto bb_id = cfg.get_bb_id(bb);
  // hoisted so the (checked) ref derefs are not repeated for every candidate
  const fir::InstrData &data = *instr.get_raw_ptr();
  const auto instr_type = data.instr_type;
  const auto subtype = data.subtype;
  const auto n_args = data.args.size();
  const auto type = instr.result_type();
  // walk up the dominator tree (nearest dominator first)
  for (u32 d = bb_id; d != cfg.entry;) {
    d = dom.idom(d);
    auto prev_bb = cfg.bbrs[d].bb;
    for (auto instr2 : prev_bb->instructions) {
      const fir::InstrData &data2 = *instr2.get_raw_ptr();
      // cheap rejects before the full comparison
      if (data2.instr_type != instr_type || data2.subtype != subtype ||
          data2.args.size() != n_args) {
        continue;
      }
      if (data.eql_expr(data2) && type == instr2.result_type()) {
        instr2->intersect_poison_flags(*instr.get_raw_ptr());
        instr->replace_all_uses(instr2.result());
        instr.destroy();
        return true;
      }
    }
  }
  return false;
}

// The clobber of a load is a store to the same object (the load is rebuilt from
// the stored value) or an earlier load of the same address in the same state.
// Replaced loads stay until the end, the cached accesses still name them.
class MemForwarder {
 public:
  explicit MemForwarder(fir::Function &f)
      : func(&f),
        cfg(AnalysisManager::cfg(f)),
        dom(AnalysisManager::dom(f)),
        info(AnalysisManager::mem_accesses(f)),
        mssa(AnalysisManager::memory_ssa(f)),
        aq(AnalysisManager::provenance(f)),
        walker(mssa, info, aq) {}

  bool run() {
    for (const auto &a : info.accesses()) {
      if (a.kind != MemAccess::Kind::Load || a.ordered || a.loc.size == 0 ||
          !a.instr->is(fir::InstrType::LoadInstr) || a.instr->Volatile ||
          !dom.is_reachable(cfg.get_bb_id(a.instr->get_parent()))) {
        continue;
      }
      const auto *meta =
          a.instr->has_access_meta ? func->get_access_meta(a.instr) : nullptr;
      const u32 c = walker.clobber(a.instr);
      if (forward_store(a, c, meta) || forward_load(a, c)) {
        dead.push_back(a.instr);
      }
    }
    for (auto load : dead) {
      ASSERT(load->get_n_uses() == 0);
      load.destroy();
    }
    return !dead.empty();
  }

 private:
  bool dominates(fir::Instr a, fir::Instr b) const {
    auto ba = a->get_parent();
    auto bb = b->get_parent();
    if (ba == bb) {
      return ba->get_instr_id(a) < ba->get_instr_id(b);
    }
    return dom.strict_dominates(ba, bb);
  }

  // The clobber if it is a plain store to the object of the load that
  // dominates it.
  const MemAccess *store_of(u32 c, const MemAccess &load) const {
    const auto &ca = mssa[c];
    if (ca.kind != MemorySSA::Kind::Def ||
        ca.access_kind != MemAccess::Kind::Store || ca.ordered ||
        !ca.instr->is(fir::InstrType::StoreInstr) || ca.instr->Volatile) {
      return nullptr;
    }
    const auto *s = info.find(ca.instr);
    if (s == nullptr || !s->loc.ptr.same_base(load.loc.ptr) ||
        !dominates(ca.instr, load.instr)) {
      return nullptr;
    }
    return s;
  }

  bool forward_store(const MemAccess &load, u32 c,
                     const fir::AccessMeta *meta) {
    const auto *s = store_of(c, load);
    if (s == nullptr) {
      return false;
    }
    const i64 rel = load.loc.ptr.offset - s->loc.ptr.offset;
    auto sty = s->instr->result_type();
    std::optional<fir::ValueR> v;
    if (rel == 0) {
      v = forward_exact(s->instr, load.instr);
    }
    if (!v && sty->is_vec() && !load.instr->result_type()->is_vec() &&
        rel >= 0) {
      v = forward_lane(s->instr, load.instr, rel);
    }
    if (!v && sty->is_vec() && load.instr->result_type()->is_vec()) {
      v = forward_half(s->instr, load.instr, rel);
    }
    if (!v) {
      v = forward_int(load, c, meta);
    }
    if (!v) {
      return false;
    }
    fir::Instr l = load.instr;
    l->replace_all_uses(*v);
    return true;
  }

  // same address, the load may see the stored bytes as another type
  std::optional<fir::ValueR> forward_exact(fir::Instr store, fir::Instr load) {
    auto t1 = store->result_type();
    auto t2 = load->result_type();
    auto *ctx = func->ctx;
    if (t1 == t2) {
      return store->args[1];
    }
    fir::Builder buh{load};
    if (t1->get_bitwidth() == t2->get_bitwidth()) {
      return buh.build_conversion_op(store->args[1], t2,
                                     fir::ConversionSubType::BitCast);
    }
    if (t1->is_vec() && !t2->is_vec() &&
        t1->as_vec().bitwidth == t2->get_bitwidth()) {
      return lane_of(buh, store->args[1], t1->as_vec(), 0, t2);
    }
    // narrower than the vector but wider than a lane, the low bytes are lane 0
    // of the vector reinterpreted with lanes of that width
    if (t1->is_vec() && (t2->is_int() || t2->is_float()) &&
        t2->get_bitwidth() > t1->as_vec().bitwidth &&
        t2->get_bitwidth() < t1->get_bitwidth() &&
        t1->get_bitwidth() % t2->get_bitwidth() == 0) {
      auto lane_kind = t2->is_float() ? fir::VectorType::SubType::Floating
                                      : fir::VectorType::SubType::Integer;
      auto cast_ty = ctx->get_vec_type(lane_kind, t2->get_bitwidth(),
                                       t1->get_bitwidth() / t2->get_bitwidth());
      auto casted = buh.build_conversion_op(store->args[1], cast_ty,
                                            fir::ConversionSubType::BitCast);
      fir::ValueR index0[1] = {fir::ValueR{ctx->get_constant_int(0, 32)}};
      return buh.build_extract_element(casted, index0, t2);
    }
    return std::nullopt;
  }

  // lane index of the vector, the load may see the lane as another type of the
  // same size
  fir::ValueR lane_of(fir::Builder &buh, fir::ValueR vec,
                      const fir::VectorType &vt, i64 index,
                      fir::TypeR load_ty) {
    auto *ctx = func->ctx;
    const bool lane_is_float = vt.type == fir::VectorType::SubType::Floating;
    fir::TypeR lane_ty =
        lane_is_float == load_ty->is_float()
            ? load_ty
            : (lane_is_float ? fir::TypeR{ctx->get_float_type(vt.bitwidth)}
                             : fir::TypeR{ctx->get_int_type(vt.bitwidth)});
    fir::ValueR indices[1] = {fir::ValueR{ctx->get_constant_int(index, 32)}};
    fir::ValueR r = buh.build_extract_element(vec, indices, lane_ty);
    if (lane_ty != load_ty) {
      r = buh.build_conversion_op(r, load_ty, fir::ConversionSubType::BitCast);
    }
    return r;
  }

  // a scalar load of one lane of a stored vector
  std::optional<fir::ValueR> forward_lane(fir::Instr store, fir::Instr load,
                                          i64 offset) {
    const auto &store_ty = store->result_type()->as_vec();
    auto load_ty = load->result_type();
    if (load_ty->get_bitwidth() != store_ty.bitwidth ||
        offset * 8 % store_ty.bitwidth != 0) {
      return std::nullopt;
    }
    const i64 index = offset / ((store_ty.bitwidth + 7) / 8);
    if (index >= store_ty.member_number) {
      return std::nullopt;
    }
    fir::Builder buh{load};
    return lane_of(buh, store->args[1], store_ty, index, load_ty);
  }

  // a vector load of the low or high half of a stored vector
  std::optional<fir::ValueR> forward_half(fir::Instr store, fir::Instr load,
                                          i64 offset) {
    const auto &store_ty = store->result_type()->as_vec();
    auto load_ty = load->result_type();
    if (load_ty->get_bitwidth() * 2 != store_ty.get_bitwidth()) {
      return std::nullopt;
    }
    const i64 half_bytes = load_ty->get_bitwidth() / 8;
    if (offset != 0 && offset != half_bytes) {
      return std::nullopt;
    }
    fir::Builder buh{load};
    return buh.build_vector_op(store->args[1], load_ty,
                               offset == 0 ? fir::VectorISubType::ExtractLow
                                           : fir::VectorISubType::ExtractHigh);
  }

  // Rebuilds an integer load from the integer stores into the same object that
  // are narrower than the load (they tile it) or wider than it (it is a
  // slice). Little endian byte order.
  std::optional<fir::ValueR> forward_int(const MemAccess &load, u32 c,
                                         const fir::AccessMeta *meta) {
    constexpr u32 MaxPieces = 8;
    auto lty = load.instr->result_type();
    if (!lty->is_int() || lty->get_bitwidth() % 8 != 0 ||
        lty->get_bitwidth() > 64) {
      return std::nullopt;
    }
    auto *ctx = func->ctx;
    const i64 lsize = lty->get_bitwidth() / 8;
    const u64 full = (1ULL << lsize) - 1;
    struct Piece {
      fir::Instr store;
      i64 off;
    };
    TVec<Piece> pieces;
    u64 mask = 0;
    for (u32 n = 0; n < MaxPieces; n++) {
      const auto *s = store_of(c, load);
      if (s == nullptr) {
        return std::nullopt;
      }
      auto sty = s->instr->result_type();
      const i64 ssize = sty->get_size();
      const i64 soff = s->loc.ptr.offset - load.loc.ptr.offset;
      if (!sty->is_int() || sty->get_bitwidth() % 8 != 0) {
        return std::nullopt;
      }
      if (soff <= 0 && lsize <= soff + ssize) {
        if (mask != 0) {
          return std::nullopt;
        }
        fir::Builder buh{load.instr};
        fir::ValueR v = s->instr->args[1];
        if (soff != 0) {
          v = buh.build_binary_op(v,
                                  fir::ValueR{ctx->get_constant_int(
                                      -soff * 8, sty->get_bitwidth())},
                                  fir::BinaryInstrSubType::Shr);
        }
        if (sty->get_bitwidth() != lty->get_bitwidth()) {
          v = buh.build_itrunc(v, lty);
        }
        return v;
      }
      if (soff < 0 || soff + ssize > lsize) {
        return std::nullopt;
      }
      const u64 bits = ((1ULL << ssize) - 1) << soff;
      if ((mask & bits) != 0) {
        return std::nullopt;
      }
      mask |= bits;
      pieces.push_back({s->instr, soff});
      if (mask == full) {
        break;
      }
      c = walker.clobber_from(mssa[c].defining, load.loc, meta);
    }
    if (mask != full || pieces.size() < 2) {
      return std::nullopt;
    }
    fir::Builder buh{load.instr};
    fir::ValueR res;
    bool first = true;
    for (auto &p : pieces) {
      fir::ValueR v = buh.build_zext(p.store->args[1], lty);
      if (p.off != 0) {
        v = buh.build_binary_op(
            v,
            fir::ValueR{ctx->get_constant_int(p.off * 8, lty->get_bitwidth())},
            fir::BinaryInstrSubType::Shl);
      }
      res =
          first ? v : buh.build_binary_op(res, v, fir::BinaryInstrSubType::Or);
      first = false;
    }
    return res;
  }

  // An earlier load of the same address that sees the same state.
  bool forward_load(const MemAccess &load, u32 c) {
    auto ty = load.instr->result_type();
    u64 h = c;
    h = h * 0x9E3779B97F4A7C15ULL +
        ankerl::unordered_dense::hash<fir::ValueR>{}(load.loc.ptr.base);
    h = h * 0x9E3779B97F4A7C15ULL + static_cast<u64>(load.loc.ptr.offset);
    h = h * 0x9E3779B97F4A7C15ULL + load.loc.size;
    auto &bucket = seen[h];
    for (auto other : bucket) {
      const auto *o = info.find(other);
      if (o->loc.size == load.loc.size &&
          o->loc.ptr.offset == load.loc.ptr.offset &&
          o->loc.ptr.same_base(load.loc.ptr) && other->result_type() == ty &&
          dominates(other, load.instr)) {
        fir::Instr l = load.instr;
        l->replace_all_uses(other.result());
        return true;
      }
    }
    bucket.push_back(load.instr);
    return false;
  }

  fir::Function *func;
  CFG &cfg;
  Dominators &dom;
  MemAccessInfo &info;
  MemorySSA &mssa;
  AliasQuery aq;
  MemorySSAWalker walker;
  TVec<fir::Instr> dead;
  TMap<u64, TVec<fir::Instr>> seen;
};

bool apply_lvn(fir::BasicBlock bb, const CFG &cfg, const Dominators &dom,
               AliasAnalyis &aa) {
  bool modified = false;
  for (size_t i = 0; i < bb->instructions.size(); i++) {
    auto instr = bb->instructions[i];
    if (gvn_applicable(instr) && apply_gvn(instr, bb, cfg, dom)) {
      i--;
      modified = true;
      continue;
    }

    // for a pure instr only the expression matching below can fire and that
    // needs the same instr type, everything else needs a load/store as instr
    const bool pure = lvn_applicable(instr);
    const auto pure_type = instr->instr_type;
    for (size_t i2 = i + 1; i2 < bb->instructions.size(); i2++) {
      ASSERT(i2 > i);
      auto instr2 = bb->instructions[i2];
      if (pure && instr2->instr_type != pure_type) {
        continue;
      }
      if (lvn_applicable(instr2)) {
        if (eql_instr_expr(instr, instr2)) {
          instr->intersect_poison_flags(*instr2.get_raw_ptr());
          instr2->replace_all_uses(instr.result());
          ASSERT(instr2->get_n_uses() == 0);
          instr2.destroy();
          i2--;
          modified = true;
          continue;
        }
        if (instr->is(fir::InstrType::ICmp) &&
            instr2->is(fir::InstrType::ICmp) &&
            instr->args[0].eql(instr2->args[0]) &&
            instr->args[1].eql(instr2->args[1])) {
          bool opposite_eql =
              (instr->subtype == static_cast<u32>(fir::ICmpInstrSubType::EQ) &&
               instr2->subtype ==
                   static_cast<u32>(fir::ICmpInstrSubType::NE)) ||
              (instr->subtype == static_cast<u32>(fir::ICmpInstrSubType::NE) &&
               instr2->subtype == static_cast<u32>(fir::ICmpInstrSubType::EQ));
          if (opposite_eql) {
            fir::Builder bb{instr2};
            auto res =
                bb.build_unary_op(instr.result(), fir::UnaryInstrSubType::Not);
            instr2->replace_all_uses(res);
            instr2.destroy();
            i2--;
            modified = true;
            continue;
          }
        }
      }

      // if we store and afterwards store to the same address
      //  and there is nobody loading that memory inbetween we can
      //  delete the first store
      if (instr->is(fir::InstrType::StoreInstr) &&
          instr2->is(fir::InstrType::StoreInstr) &&
          instr->result_type()->get_bitwidth() ==
              instr2.result_type()->get_bitwidth() &&
          instr->get_arg(0) == instr2->get_arg(0) && !instr->Volatile &&
          !instr2->Volatile) {
        bool pot_load_between = is_pot_load_between(
            bb, instr->args[0], instr->result_type()->get_size(), i + 1, i2, aa,
            instr);
        if (!pot_load_between) {
          instr.destroy();
          i--;
          modified = true;
          break;
        }
      }

      // merge two stores of for example i8 into 1 i16 store
      if (instr->is(fir::InstrType::StoreInstr) &&
          instr2->is(fir::InstrType::StoreInstr) &&
          instr->result_type()->get_bitwidth() ==
              instr2.result_type()->get_bitwidth() &&
          instr->args[0].is_instr() && instr2->args[0].is_instr() &&
          !instr->Volatile && !instr2->Volatile) {
        auto arg1 = instr->args[0].as_instr();
        auto arg2 = instr2->args[0].as_instr();
        auto old_width = instr->result_type()->get_size();
        fir::ValueR base1_addr = instr->args[0];
        i128 base1_off = 0;
        fir::ValueR base2_addr = instr2->args[0];
        i128 base2_off = 0;
        if (arg1->is(fir::BinaryInstrSubType::PtrAdd) &&
            arg1->args[1].is_constant() &&
            arg1->args[1].as_constant()->is_int()) {
          base1_addr = arg1->args[0];
          base1_off = arg1->args[1].as_constant()->as_int();
        }
        if (arg2->is(fir::BinaryInstrSubType::PtrAdd) &&
            arg2->args[1].is_constant() &&
            arg2->args[1].as_constant()->is_int()) {
          base2_addr = arg2->args[0];
          base2_off = arg2->args[1].as_constant()->as_int();
        }
        if (instr->result_type()->is_vec() && old_width <= 16 &&
            base1_addr == base2_addr && base1_off + old_width == base2_off &&
            instr->result_type() == instr2->result_type()) {
          auto v1_type = instr->result_type()->as_vec();
          bool pot_load_between = is_pot_loadstore_between(
              bb, instr->args[0], old_width * 2, i + 1, i2, aa);
          if (!pot_load_between) {
            fir::Builder buh{instr2};
            auto *ctx = bb->get_parent()->ctx;
            auto new_type = ctx->get_vec_type(v1_type.type, v1_type.bitwidth,
                                              v1_type.member_number * 2);
            auto v1 = instr->args[1];
            auto v2 = instr2->args[1];
            auto data = buh.build_vector_op(v1, v2, new_type,
                                            fir::VectorISubType::Concat);
            buh.build_store(instr->args[0], data,
                            instr->Atomic || instr2->Atomic, false);
            instr.destroy();
            instr2.destroy();
            i--;
            modified = true;
            break;
          }
        } else if (instr->result_type()->is_float() && old_width <= 16 &&
                   base1_addr == base2_addr &&
                   base1_off + old_width == base2_off &&
                   instr->result_type() == instr2->result_type()) {
          bool pot_load_between = is_pot_loadstore_between(
              bb, instr->args[0], old_width * 2, i + 1, i2, aa);
          if (!pot_load_between) {
            fir::Builder buh{instr2};
            auto *ctx = bb->get_parent()->ctx;
            auto base_ty = instr->result_type();
            auto new_type = ctx->get_vec_type(
                fir::VectorType::SubType::Floating, base_ty->as_float(), 2);
            auto v1 = instr->args[1];
            auto v2 = instr2->args[1];
            fir::ValueR indices[1] = {
                fir::ValueR{ctx->get_constant_int(0, 32)}};
            auto res = buh.build_insert_element(
                fir::ValueR{ctx->get_poisson_value(new_type)}, v1, indices,
                new_type);
            fir::ValueR indices2[1] = {
                fir::ValueR{ctx->get_constant_int(1, 32)}};
            res = buh.build_insert_element(res, v2, indices2, new_type);
            buh.build_store(instr->args[0], res,
                            instr->Atomic || instr2->Atomic, false);
            instr.destroy();
            instr2.destroy();
            i--;
            modified = true;
            break;
          }
        }
        // merge integers into bigger integers
        if (instr->result_type()->is_int() && old_width <= 4 &&
            base1_addr == base2_addr && base1_off + old_width == base2_off &&
            instr->args[1].is_constant() && instr2->args[1].is_constant()) {
          bool pot_load_between = is_pot_loadstore_between(
              bb, instr->args[0], old_width * 2, i + 1, i2, aa);
          if (!pot_load_between) {
            fir::Builder buh{instr2};
            auto *ctx = bb->get_parent()->ctx;
            auto new_type = ctx->get_int_type(2 * old_width * 8);
            auto v1 = instr->args[1];
            auto v2 = instr2->args[1];
            auto v1_ext = buh.build_zext(v1, new_type);
            auto v2_ext = buh.build_zext(v2, new_type);
            auto shift2_val = buh.build_binary_op(
                v2_ext,
                fir::ValueR{ctx->get_constant_value(old_width * 8, new_type)},
                fir::BinaryInstrSubType::Shl);
            auto data = buh.build_binary_op(v1_ext, shift2_val,
                                            fir::BinaryInstrSubType::Or);
            buh.build_store(instr->args[0], data,
                            instr->Atomic || instr2->Atomic, false);
            instr.destroy();
            instr2.destroy();
            i--;
            modified = true;
            break;
          }
        }
        // if (instr->get_type()->is_int() && old_width == 8 &&
        //     base1_addr == base2_addr && base1_off + old_width == base2_off &&
        //     instr->args[1].is_constant() && instr2->args[1].is_constant()) {
        //   bool pot_load_between = is_pot_loadstore_between(
        //       bb, instr->args[0], old_width * 2, i + 1, i2, aa);
        //   if (!pot_load_between) {
        //     fir::Builder buh{instr2};
        //     auto *ctx = bb->get_parent()->ctx;
        //     auto v1 = instr->args[1];
        //     auto v2 = instr2->args[1];
        //     auto data = ctx->get_constant_value(
        //         {v1.as_constant(), v2.as_constant()}, instr->get_type());
        //     buh.build_store(instr->args[0], fir::ValueR{data},
        //                     instr->Atomic || instr2->Atomic, false);
        //     instr.destroy();
        //     instr2.destroy();
        //     i--;
        //     break;
        //   }
        // }
      }

      if (instr->is(fir::InstrType::LoadInstr) &&
          instr2->is(fir::InstrType::LoadInstr) &&
          instr->result_type()->get_bitwidth() ==
              instr2.result_type()->get_bitwidth()) {
        // merge 2 loads
        if (instr->args[0].is_instr() && instr2->args[0].is_instr() &&
            !instr->Volatile && !instr2->Volatile) {
          auto arg1 = instr->args[0].as_instr();
          auto arg2 = instr2->args[0].as_instr();
          auto old_width = instr->result_type()->get_size();
          fir::ValueR base1_addr = instr->args[0];
          i128 base1_off = 0;
          fir::ValueR base2_addr = instr2->args[0];
          i128 base2_off = 0;
          if (arg1->is(fir::BinaryInstrSubType::PtrAdd) &&
              arg1->args[1].is_constant() &&
              arg1->args[1].as_constant()->is_int()) {
            base1_addr = arg1->args[0];
            base1_off = arg1->args[1].as_constant()->as_int();
          }
          if (arg2->is(fir::BinaryInstrSubType::PtrAdd) &&
              arg2->args[1].is_constant() &&
              arg2->args[1].as_constant()->is_int()) {
            base2_addr = arg2->args[0];
            base2_off = arg2->args[1].as_constant()->as_int();
          }
          if (instr->result_type()->is_vec() && old_width <= 16 &&
              base1_addr == base2_addr && base1_off + old_width == base2_off &&
              instr->result_type() == instr2->result_type()) {
            auto v1_type = instr->result_type()->as_vec();
            bool pot_store_between = is_pot_store_between(
                bb, instr->args[0], old_width * 2, i + 1, i2, aa);
            if (!pot_store_between) {
              fir::Builder buh{instr};
              auto *ctx = bb->get_parent()->ctx;
              auto new_type = ctx->get_vec_type(v1_type.type, v1_type.bitwidth,
                                                v1_type.member_number * 2);
              auto loaded_data =
                  buh.build_load(new_type, instr->args[0],
                                 instr->Atomic || instr2->Atomic, false);
              auto data1 =
                  buh.build_vector_op(loaded_data, instr->result_type(),
                                      fir::VectorISubType::ExtractLow);
              auto data2 =
                  buh.build_vector_op(loaded_data, instr->result_type(),
                                      fir::VectorISubType::ExtractHigh);
              instr->replace_all_uses(data1);
              instr2->replace_all_uses(data2);
              instr.destroy();
              instr2.destroy();
              i--;
              modified = true;
              break;
            }
          }
          // if (instr->get_type()->is_int() && old_width <= 4 &&
          //     base1_addr == base2_addr && base1_off + old_width == base2_off)
          //     {
          //   bool pot_store_between = is_pot_store_between(
          //       bb, instr->args[0], old_width * 2, i + 1, i2, aa);
          //   if (!pot_store_between) {
          //     // fmt::println("{:cd}", bb);
          //     fir::Builder buh{instr};
          //     auto *ctx = bb->get_parent()->ctx;
          //     auto new_type = ctx->get_int_type(2 * old_width * 8);
          //     auto loaded_data =
          //         buh.build_load(new_type, fir::ValueR{arg1},
          //                        instr->Atomic || instr2->Atomic, false);
          //     auto v1_val = buh.build_itrunc(loaded_data, instr->get_type());
          //     auto shift2_val = buh.build_binary_op(
          //         loaded_data,
          //         fir::ValueR{ctx->get_constant_value(old_width * 8,
          //         new_type)}, fir::BinaryInstrSubType::Shr);
          //     auto v2_val = buh.build_itrunc(shift2_val, instr->get_type());
          //     instr->replace_all_uses(v1_val);
          //     instr2->replace_all_uses(v2_val);
          //     instr.destroy();
          //     instr2.destroy();
          //     i--;
          //     break;
          //   }
          // }
        }
      }
    }
  }
  return modified;
}

}  // namespace

PreservedAnalysis LVN::apply(fir::Context & /*unused*/, fir::Function &func) {
  ZoneScopedNC("LVN", COLOR_OPTIMF);
  CFG &cfg = AnalysisManager::cfg(func);
  Dominators &dom = AnalysisManager::dom(func);
  bool modified = MemForwarder{func}.run();
  if (modified) {
    AnalysisManager::drop_memory(func);
  }
  AliasAnalyis aa;
  for (auto bb : func.basic_blocks) {
    modified |= apply_lvn(bb, cfg, dom, aa);
  }
  // only instructions change, never blocks or edges
  return modified ? PreservedAnalysis::cfg_only() : PreservedAnalysis::all();
}

}  // namespace foptim::optim
