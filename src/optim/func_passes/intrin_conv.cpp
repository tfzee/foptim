#include "intrin_conv.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include <algorithm>
#include <bit>
#include <optional>

namespace foptim::optim {

namespace {

// the block args passed on the loop exit edge, empty optional if one is
// defined inside the loop (the loop is removed so it can not be forwarded)
std::optional<TVec<fir::ValueR>>
exit_edge_args(fir::Instr term, fir::BasicBlock loop_bb, size_t exit_idx) {
  TVec<fir::ValueR> res;
  for (auto arg : term->get_bb_args()[exit_idx].args) {
    if ((arg.is_instr() && arg.as_instr()->get_parent() == loop_bb) ||
        (arg.is_bb_arg() && arg.as_bb_arg()->get_parent() == loop_bb)) {
      return std::nullopt;
    }
    res.push_back(arg);
  }
  return res;
}

// A pointer of a single block loop as base + off0 + stride * k in iteration k.
struct PtrForm {
  fir::ValueR base;
  i128 off0 = 0;
  i128 stride = 0;
};

struct LinearIdx {
  i128 scale = 0;
  i128 off = 0;
};

// the induction variable is the first header argument
struct LoopCtx {
  fir::BasicBlock bb;
  fir::BBArgument iv;
  i128 lower;
  TVec<fir::ValueR> incoming;
};

 std::optional<LinearIdx> linear_idx(fir::ValueR v, const LoopCtx &lc) {
  if (v.is_bb_arg() && v.as_bb_arg() == lc.iv) {
    return LinearIdx{.scale = 1, .off = 0};
  }
  if (v.is_constant_int()) {
    return LinearIdx{.scale = 0, .off = v.as_constant()->as_int()};
  }
  if (!v.is_instr() || v.as_instr()->get_parent() != lc.bb) {
    return std::nullopt;
  }
  auto i = v.as_instr();
  if (i->is(fir::BinaryInstrSubType::IntAdd) ||
      i->is(fir::BinaryInstrSubType::IntSub)) {
    auto a = linear_idx(i->args[0], lc);
    auto b = linear_idx(i->args[1], lc);
    if (!a || !b) {
      return std::nullopt;
    }
    const i128 sign = i->is(fir::BinaryInstrSubType::IntAdd) ? 1 : -1;
    return LinearIdx{.scale = a->scale + (sign * b->scale),
                     .off = a->off + (sign * b->off)};
  }
  if (i->is(fir::BinaryInstrSubType::IntMul)) {
    auto a = linear_idx(i->args[0], lc);
    auto b = linear_idx(i->args[1], lc);
    if (!a || !b || (a->scale != 0 && b->scale != 0)) {
      return std::nullopt;
    }
    if (a->scale == 0) {
      std::swap(a, b);
    }
    return LinearIdx{.scale = a->scale * b->off, .off = a->off * b->off};
  }
  return std::nullopt;
}

std::optional<PtrForm> ptr_form(fir::ValueR v, const LoopCtx &lc) {
  if (v.is_instr() && v.as_instr()->get_parent() == lc.bb) {
    auto i = v.as_instr();
    if (!i->is(fir::BinaryInstrSubType::PtrAdd)) {
      return std::nullopt;
    }
    auto p = ptr_form(i->args[0], lc);
    auto idx = linear_idx(i->args[1], lc);
    if (!p || !idx) {
      return std::nullopt;
    }
    p->off0 += idx->scale * lc.lower + idx->off;
    p->stride += idx->scale;
    return p;
  }
  if (v.is_bb_arg() && v.as_bb_arg()->get_parent() == lc.bb) {
    if (v.as_bb_arg() == lc.iv) {
      return std::nullopt;
    }
    // a pointer that is bumped by a constant every iteration
    size_t pos = 0;
    while (lc.bb->args[pos] != v.as_bb_arg()) {
      pos++;
    }
    auto term = lc.bb->get_terminator();
    for (size_t e = 0; e < term->bbs.size(); e++) {
      if (term->bbs[e].bb != lc.bb) {
        continue;
      }
      auto back = term->bbs[e].args[pos];
      if (!back.is_instr() ||
          !back.as_instr()->is(fir::BinaryInstrSubType::PtrAdd) ||
          back.as_instr()->args[0] != v ||
          !back.as_instr()->args[1].is_constant_int()) {
        return std::nullopt;
      }
      return PtrForm{.base = lc.incoming[pos],
                     .off0 = 0,
                     .stride =
                         back.as_instr()->args[1].as_constant()->as_int()};
    }
    return std::nullopt;
  }
  return PtrForm{.base = v};
}

enum class CopyKind : u8 { Memcpy, Memmove, None };

 CopyKind classify_copy(const PtrForm &dst, const PtrForm &src,
                              i128 bytes, i128 elem_size, AliasAnalyis &aa) {
  if (dst.stride != elem_size || src.stride != elem_size) {
    return CopyKind::None;
  }
  if (dst.base == src.base) {
    if (dst.off0 + bytes <= src.off0 || src.off0 + bytes <= dst.off0) {
      return CopyKind::Memcpy;
    }
    // dst in front of src is a forward memmove
    return dst.off0 < src.off0 ? CopyKind::Memmove : CopyKind::None;
  }
  const auto extent = [&](const PtrForm &f) {
    return f.off0 >= 0 ? static_cast<size_t>(f.off0 + bytes) : size_t{0};
  };
  return aa.alias(dst.base, src.base, extent(dst), extent(src)) ==
                 AliasAnalyis::AAResult::NoAlias
             ? CopyKind::Memcpy
             : CopyKind::None;
}

// x if v is -x
 fir::ValueR negated_operand(fir::ValueR v) {
  if (!v.is_instr()) {
    return {};
  }
  auto i = v.as_instr();
  const auto is_zero = [](fir::ValueR z) {
    return z.is_constant() &&
           ((z.as_constant()->is_int() && z.as_constant()->as_int() == 0) ||
            (z.as_constant()->is_float() && z.as_constant()->as_f64() == 0));
  };
  if (i->is(fir::UnaryInstrSubType::IntNeg) ||
      i->is(fir::UnaryInstrSubType::FloatNeg)) {
    return i->args[0];
  }
  if ((i->is(fir::BinaryInstrSubType::IntSub) ||
       i->is(fir::BinaryInstrSubType::FloatSub)) &&
      is_zero(i->args[0])) {
    return i->args[1];
  }
  return {};
}

// Whether the select condition being true means the value is negative, for
// value cmp c. Nothing if the compare says nothing about the sign.
 std::optional<bool> true_means_negative(fir::ICmpInstrSubType pred,
                                               i128 c) {
  using fir::ICmpInstrSubType;
  switch (pred) {
  case ICmpInstrSubType::SLT:
    return c == 0 || c == 1 ? std::optional{true} : std::nullopt;
  case ICmpInstrSubType::SLE:
    return c == 0 || c == -1 ? std::optional{true} : std::nullopt;
  case ICmpInstrSubType::SGT:
    return c == 0 || c == -1 ? std::optional{false} : std::nullopt;
  case ICmpInstrSubType::SGE:
    return c == 0 || c == 1 ? std::optional{false} : std::nullopt;
  default:
    return std::nullopt;
  }
}

// The fabs arm is only exact when the other arm is taken for strictly positive
// values and the compare is false for NaN exactly when the fabs arm is the one
// taken for it.
 std::optional<bool> true_means_negative(fir::FCmpInstrSubType pred,
                                               bool fabs_arm) {
  using fir::FCmpInstrSubType;
  switch (pred) {
  case FCmpInstrSubType::OLT:
  case FCmpInstrSubType::ULT:
  case FCmpInstrSubType::OLE:
    return fabs_arm ? std::nullopt : std::optional{true};
  case FCmpInstrSubType::ULE:
    return true;
  case FCmpInstrSubType::OGT:
    return false;
  case FCmpInstrSubType::UGT:
  case FCmpInstrSubType::OGE:
  case FCmpInstrSubType::UGE:
    return fabs_arm ? std::nullopt : std::optional{false};
  default:
    return std::nullopt;
  }
}

// select (x < 0), -x, x and its variants, also with a fabs of x as the arm for
// negative values
 fir::ValueR select_abs_operand(fir::Instr sel, bool exact_neg) {
  if (!sel->args[0].is_instr()) {
    return {};
  }
  auto cmp = sel->args[0].as_instr();
  const bool is_float = cmp->is(fir::InstrType::FCmp);
  if (!is_float && !cmp->is(fir::InstrType::ICmp)) {
    return {};
  }
  const bool zero_rhs = cmp->args[1].is_constant() &&
                        cmp->args[1].as_constant()->is_float() &&
                        cmp->args[1].as_constant()->as_f64() == 0;
  if (is_float ? !zero_rhs : !cmp->args[1].is_constant_int()) {
    return {};
  }
  auto x = cmp->args[0];
  // a compare of a sign extended narrow value is a compare of the value
  if (!is_float && x.is_instr() && x.as_instr()->is(fir::InstrType::SExt)) {
    x = x.as_instr()->args[0];
  }
  if (x.get_type() != sel->get_type()) {
    return {};
  }
  const auto arm_form = [&](fir::ValueR arm) {
    if (negated_operand(arm) == x) {
      return 1;
    }
    if (is_float && arm.is_instr() &&
        arm.as_instr()->is(fir::IntrinsicSubType::FAbs) &&
        arm.as_instr()->args[0] == x) {
      return 2;
    }
    return 0;
  };
  for (const bool neg_is_true_arm : {true, false}) {
    const int form = arm_form(sel->args[neg_is_true_arm ? 1 : 2]);
    if (sel->args[neg_is_true_arm ? 2 : 1] != x || form == 0) {
      continue;
    }
    // -x is not fabs(x) for zeros and NaNs
    if (is_float && form == 1 && !exact_neg) {
      continue;
    }
    const auto neg_when_true =
        is_float
            ? true_means_negative(
                  static_cast<fir::FCmpInstrSubType>(cmp->subtype), form == 2)
            : true_means_negative(
                  static_cast<fir::ICmpInstrSubType>(cmp->subtype),
                  cmp->args[1].as_constant()->as_int());
    if (neg_when_true == neg_is_true_arm) {
      return x;
    }
  }
  return {};
}

// m is x >> (bitwidth - 1), narrow values are shifted as sign extended i32
 bool is_sign_mask(fir::ValueR m, fir::ValueR x) {
  if (!m.is_instr() || !x.get_type()->is_int()) {
    return false;
  }
  const auto width = static_cast<i128>(x.get_type()->get_bitwidth());
  auto i = m.as_instr();
  fir::ValueR shifted = m;
  if (i->is(fir::InstrType::ITrunc)) {
    shifted = i->args[0];
  }
  if (!shifted.is_instr() ||
      !shifted.as_instr()->is(fir::BinaryInstrSubType::AShr)) {
    return false;
  }
  auto shr = shifted.as_instr();
  auto src = shr->args[0];
  if (shifted != m && src.is_instr() &&
      src.as_instr()->is(fir::InstrType::SExt)) {
    src = src.as_instr()->args[0];
  }
  const i128 shift_width = shr->get_type()->get_bitwidth();
  return src == x && shr->args[1].is_constant_int() &&
         shr->args[1].as_constant()->as_int() >= width - 1 &&
         shr->args[1].as_constant()->as_int() < shift_width;
}

// x if p is x op m or m op x with m the sign mask of x
 fir::ValueR operand_with_sign_mask(fir::ValueR p, fir::ValueR m) {
  auto u = p.as_instr()->args[0];
  auto v = p.as_instr()->args[1];
  if (m == v && is_sign_mask(v, u)) {
    return u;
  }
  if (m == u && is_sign_mask(u, v)) {
    return v;
  }
  return {};
}

 bool is_instr_of(fir::ValueR v, fir::BinaryInstrSubType sub) {
  return v.is_instr() && v.as_instr()->is(sub);
}

// The branch free forms (x + m) ^ m, (x ^ m) - m and (x ^ m) + (m & 1) with
// m = x >> (bitwidth - 1).
 fir::ValueR bit_trick_abs_operand(fir::Instr instr) {
  using Sub = fir::BinaryInstrSubType;
  if (instr->is(Sub::Xor)) {
    for (size_t k = 0; k < 2; k++) {
      if (instr->args[k].is_instr(Sub::IntAdd)) {
        auto x = operand_with_sign_mask(instr->args[k], instr->args[1 - k]);
        if (!x.is_invalid()) {
          return x;
        }
      }
    }
  } else if (instr->is(Sub::IntSub)) {
    if (instr->args[0].is_instr(Sub::Xor)) {
      return operand_with_sign_mask(instr->args[0], instr->args[1]);
    }
  } else if (instr->is(Sub::IntAdd)) {
    for (size_t k = 0; k < 2; k++) {
      auto p = instr->args[k];
      auto q = instr->args[1 - k];
      if (!p.is_instr(Sub::Xor) || !q.is_instr(Sub::And)) {
        continue;
      }
      for (size_t j = 0; j < 2; j++) {
        auto one = q.as_instr()->args[1 - j];
        if (one.is_constant_int() && one.as_constant()->as_int() == 1) {
          auto x = operand_with_sign_mask(p, q.as_instr()->args[j]);
          if (!x.is_invalid()) {
            return x;
          }
        }
      }
    }
  }
  return {};
}

// and with the mask that clears the sign bit of a float
 bool is_fabs_mask_and(fir::Instr instr) {
  if (!instr->is(fir::BinaryInstrSubType::And) ||
      !instr->get_type()->is_float() || !instr->args[1].is_constant() ||
      !instr->args[1].as_constant()->is_float()) {
    return false;
  }
  auto c = instr->args[1].as_constant();
  return instr->get_type()->get_bitwidth() == 32
             ? std::bit_cast<u32>(c->as_f32()) == 0x7FFFFFFFU
             : std::bit_cast<u64>(c->as_f64()) == 0x7FFFFFFFFFFFFFFFULL;
}

}; // namespace

// detect single basicblock memset/memcpy
bool match_memsetcpy(fir::Function &func) {
  /*
  something like
  loop(0x61136f0cda80{}: i64 NUSES: 2):
   0x61136f0e9b88: ptr = PtrAdd(0x61136f0c9e58, 0x61136f0cda80)
   0x61136f0e9c10: i8 = Store(0x61136f0e9b88, 0:i8){}
   0x61136f0e9eb8: i64 = IntAdd(0x61136f0cda80, 1:i64){}
   0x61136f0c9c38: i1 = IntULE(0x61136f0e9eb8, 15:i64){}
   0x61136f0c9b28: i1 = Not(0x61136f0c9c38){}
   0x61136f0c9bb0: () = CondBranch(0x61136f0c9b28){}
  */
  (void)func;
  CFG cfg{func};
  DominatorTree dom2{cfg};
  LoopInfoAnalysis linfo{dom2};
  LoopRangeAnalysis range{};
  AliasAnalyis aa;

  if (linfo.info.empty()) {
    return false;
  }
  for (auto &l : linfo.info) {
    if (l.body_nodes.size() != 1 || l.body_nodes[0] != l.head) {
      continue;
    }
    if (!range.update(cfg, l)) {
      continue;
    }
    if (!range.known_lower || !range.known_upper) {
      continue;
    }
    // the element count and the contiguous layout only hold for a step of 1
    if (range.a != 1) {
      continue;
    }
    auto r = range.upper_bound - range.lower_bound;
    auto loop_bb = cfg.bbrs[l.head].bb;
    LoopCtx lc{.bb = loop_bb,
               .iv = loop_bb->args[0],
               .lower = range.lower_bound,
               .incoming = {}};
    for (u32 p : cfg.bbrs[l.head].pred) {
      if (std::ranges::find(l.body_nodes, p) == l.body_nodes.end()) {
        auto pred_term = cfg.bbrs[p].bb->get_terminator();
        auto edge = pred_term.get_bb_id(loop_bb);
        for (auto a : pred_term->bbs[edge].args) {
          lc.incoming.push_back(a);
        }
        break;
      }
    }
    if (lc.incoming.size() != loop_bb->args.size()) {
      continue;
    }

    // if all vlaues in this loop arent used afterwards && we dont load we only
    // have a store of a constant we can conver it
    bool outside_use = false;
    bool other_effects = false;
    fir::Instr store_instr{fir::Instr::invalid()};
    fir::Instr load_instr{fir::Instr::invalid()};
    for (auto i : loop_bb->instructions) {
      for (auto use : i->uses) {
        if (use.user->get_parent() != loop_bb) {
          outside_use = true;
          break;
        }
      }
      if (i->pot_modifies_mem() || i->pot_reads_mem() ||
          i->has_pot_sideeffects() || i->is_critical()) {
        if (i->is(fir::InstrType::StoreInstr) && !store_instr.is_valid()) {
          store_instr = i;
          continue;
        }
        if (i->is(fir::InstrType::LoadInstr) && !load_instr.is_valid()) {
          load_instr = i;
          continue;
        }
        if (i->is(fir::InstrType::CondBranchInstr) ||
            i->is(fir::InstrType::BranchInstr) ||
            i->is(fir::InstrType::SwitchInstr) ||
            i->is(fir::InstrType::ReturnInstr)) {
          continue;
        }
        other_effects = true;
        break;
      }
      if (outside_use) {
        break;
      }
    }

    if (other_effects || outside_use || !store_instr.is_valid()) {
      continue;
    }
    if (store_instr->Volatile || store_instr->Atomic) {
      continue;
    }

    // if we get load into store then we can do memcpy instead
    if (load_instr.is_valid()) {
      if (load_instr->get_type() != store_instr.get_type() ||
          store_instr->args[1] != fir::ValueR{load_instr} ||
          load_instr->Volatile || load_instr->Atomic) {
        continue;
      }
      auto dst_form = ptr_form(store_instr->args[0], lc);
      auto src_form = ptr_form(load_instr->args[0], lc);
      if (!dst_form || !src_form) {
        continue;
      }
      const i128 elem_size = load_instr->get_type()->get_size();
      const auto kind =
          classify_copy(*dst_form, *src_form, r * elem_size, elem_size, aa);
      if (kind == CopyKind::None) {
        continue;
      }
      fir::Builder buh{store_instr};
      auto old_term = loop_bb->get_terminator();
      ASSERT(old_term->bbs.size() == 2);
      const size_t exit_idx = old_term->bbs[0].bb == loop_bb ? 1 : 0;
      auto exit_args = exit_edge_args(old_term, loop_bb, exit_idx);
      if (!exit_args.has_value()) {
        continue;
      }
      buh.at_end(loop_bb);
      auto memcpy_instr = buh.build_intrinsic(
          store_instr->args[0], load_instr->args[0],
          fir::ValueR{func.ctx->get_constant_int(r, 64)},
          kind == CopyKind::Memcpy ? fir::IntrinsicSubType::Memcpy
                                   : fir::IntrinsicSubType::Memmove,
          func.ctx->get_void_type());
      // the count is in elements of this type
      memcpy_instr.as_instr()->extra_type = load_instr->get_type();
      fir::BasicBlock target_bb{fir::BasicBlock::invalid()};
      if (old_term->bbs[0].bb == loop_bb) {
        target_bb = old_term->bbs[1].bb;
      } else if (old_term->bbs[1].bb == loop_bb) {
        target_bb = old_term->bbs[0].bb;
      }
      auto new_br = buh.build_branch(target_bb);
      for (auto arg : *exit_args) {
        new_br.add_bb_arg(0, arg);
      }
      load_instr.destroy();
      store_instr.destroy();
      old_term.destroy();
      return true;
    }
    // we need to store a constant
    auto store_val = store_instr->args[1];
    bool is_constant = store_val.is_constant();
    bool is_instr_loop_constant_value =
        store_val.is_instr() && loop_bb != store_val.as_instr()->get_parent();
    bool is_bb_arg_loop_constant_value =
        store_val.is_bb_arg() && loop_bb != store_val.as_bb_arg()->get_parent();
    if (!is_constant && !is_instr_loop_constant_value &&
        !is_bb_arg_loop_constant_value) {
      continue;
    }
    auto dst_form = ptr_form(store_instr->args[0], lc);
    if (!dst_form || dst_form->stride !=
                         static_cast<i128>(store_val.get_type()->get_size())) {
      continue;
    }
    fir::Builder buh{store_instr};
    auto old_term = loop_bb->get_terminator();
    ASSERT(old_term->bbs.size() == 2);
    const size_t exit_idx = old_term->bbs[0].bb == loop_bb ? 1 : 0;
    auto exit_args = exit_edge_args(old_term, loop_bb, exit_idx);
    if (!exit_args.has_value()) {
      continue;
    }
    buh.at_end(loop_bb);
    buh.build_intrinsic(store_instr->args[0], store_instr->args[1],
                        fir::ValueR{func.ctx->get_constant_int(r, 64)},
                        fir::IntrinsicSubType::Memset,
                        func.ctx->get_void_type());
    fir::BasicBlock target_bb{fir::BasicBlock::invalid()};
    if (old_term->bbs[0].bb == loop_bb) {
      target_bb = old_term->bbs[1].bb;
    } else if (old_term->bbs[1].bb == loop_bb) {
      target_bb = old_term->bbs[0].bb;
    }
    auto new_br = buh.build_branch(target_bb);
    for (auto arg : *exit_args) {
      new_br.add_bb_arg(0, arg);
    }
    store_instr.destroy();
    old_term.destroy();
    return true;
  }
  return false;
}

// select, bit trick and sign mask forms of abs and fabs
bool match_abs(fir::Context &ctx, fir::Function &func) {
  TVec<fir::Instr> candidates;
  for (auto bb : func.basic_blocks) {
    for (auto instr : bb->get_instrs()) {
      if (instr->is(fir::InstrType::SelectInstr) ||
          instr->is(fir::InstrType::BinaryInstr)) {
        candidates.push_back(instr);
      }
    }
  }
  // selects last, they can pick up the fabs made from an and
  std::ranges::stable_partition(candidates, [](fir::Instr i) {
    return !i->is(fir::InstrType::SelectInstr);
  });
  const auto &flt = ctx.config->optim.fltOpt;
  const bool exact_neg = flt.no_signed_zeros && flt.no_nans;
  bool modified = false;
  for (auto instr : candidates) {
    fir::ValueR x;
    bool is_float = false;
    if (instr->is(fir::InstrType::SelectInstr)) {
      x = select_abs_operand(instr, exact_neg);
      is_float = instr->get_type()->is_float();
    } else if (is_fabs_mask_and(instr)) {
      x = instr->args[0];
      is_float = true;
    } else {
      x = bit_trick_abs_operand(instr);
    }
    if (x.is_invalid()) {
      continue;
    }
    fir::Builder b{instr};
    auto res = b.build_intrinsic(x, is_float ? fir::IntrinsicSubType::FAbs
                                             : fir::IntrinsicSubType::Abs);
    instr->replace_all_uses(res);
    instr.destroy();
    modified = true;
  }
  return modified;
}

} // namespace foptim::optim
