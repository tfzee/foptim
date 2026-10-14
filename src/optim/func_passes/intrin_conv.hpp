#pragma once
#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/loop_analysis.hpp"
#include <fmt/base.h>
#include <optional>

/*
Try to convert intrinsic like patterns into the actual intrinsics this includes
not proper intrinsics like memcpy/memset etc
*/

namespace foptim::optim {

namespace {

// the block args passed on the loop exit edge, empty optional if one is
// defined inside the loop (the loop is removed so it can not be forwarded)
std::optional<TVec<fir::ValueR>> exit_edge_args(fir::Instr term,
                                                fir::BasicBlock loop_bb,
                                                size_t exit_idx) {
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

static std::optional<LinearIdx> linear_idx(fir::ValueR v, const LoopCtx &lc) {
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
    return LinearIdx{.scale = a->scale + sign * b->scale,
                     .off = a->off + sign * b->off};
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

static std::optional<PtrForm> ptr_form(fir::ValueR v, const LoopCtx &lc) {
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
                     .stride = back.as_instr()->args[1].as_constant()->as_int()};
    }
    return std::nullopt;
  }
  return PtrForm{.base = v};
}

enum class CopyKind : u8 { Memcpy, Memmove, None };

static CopyKind classify_copy(const PtrForm &dst, const PtrForm &src, i128 bytes,
                       i128 elem_size, AliasAnalyis &aa) {
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
    if (!dst_form ||
        dst_form->stride !=
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

}; // namespace

class IntrinConv final : public FunctionPass {
public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {

    (void)ctx;
    (void)func;
    ZoneScopedNC("IntrinConv", COLOR_OPTIMF);
    bool modified = match_memsetcpy(func);
    return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
  }
};

} // namespace foptim::optim
