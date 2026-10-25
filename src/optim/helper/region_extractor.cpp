#include "region_extractor.hpp"

#include <span>

#include "ir/basic_block.hpp"
#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/mem/alloca_escape.hpp"
#include "optim/analysis/mem/mem_access.hpp"
#include "utils/set.hpp"

namespace foptim::optim {

std::optional<Region> find_region(fir::Function &f, fir::BasicBlock entry,
                                  const CFG &cfg, const RegionLimits &limits,
                                  RegionReject *why) {
  auto reject = [&](RegionReject r) -> std::optional<Region> {
    if (why != nullptr) {
      *why = r;
    }
    return std::nullopt;
  };
  if (why != nullptr) {
    *why = RegionReject::None;
  }
  const u32 n = static_cast<u32>(cfg.bbrs.size());
  const u32 entry_id = cfg.get_bb_id(entry);
  if (entry == f.get_entry() || entry_id == cfg.entry) {
    return reject(RegionReject::FunctionEntry);
  }

  TVec<u8> in(n, 0);
  TVec<u32> work{entry_id};
  in[entry_id] = 1;
  while (!work.empty()) {
    const u32 b = work.back();
    work.pop_back();
    for (const u32 s : cfg.bbrs[b].succ) {
      if (in[s] == 0U) {
        in[s] = 1;
        work.push_back(s);
      }
    }
  }
  if (in[cfg.entry] != 0U) {
    return reject(RegionReject::SecondEntry);
  }
  Region r;
  r.entry = entry;
  r.blocks.push_back(entry);
  for (u32 b = 0; b < n; b++) {
    if (in[b] == 0U) {
      continue;
    }
    if (b != entry_id) {
      for (const u32 p : cfg.bbrs[b].pred) {
        if (in[p] == 0U) {
          return reject(RegionReject::SecondEntry);
        }
      }
      r.blocks.push_back(cfg.bbrs[b].bb);
    }
  }

  TSet<fir::BasicBlock> region_bbs;
  for (auto bb : r.blocks) {
    region_bbs.insert(bb);
  }
  TSet<fir::ValueR> seen;
  auto use = [&](fir::ValueR v) {
    fir::BasicBlock def_bb{fir::BasicBlock::invalid()};
    if (v.is_instr()) {
      def_bb = v.as_instr()->get_parent();
    } else if (v.is_bb_arg()) {
      def_bb = v.as_bb_arg()->get_parent();
    } else {
      return;
    }
    if (!region_bbs.contains(def_bb) && seen.insert(v).second) {
      r.live_in.push_back(v);
    }
  };
  auto used_outside = [&](const auto &uses) {
    for (const auto &u : uses) {
      if (!region_bbs.contains(u.user->get_parent())) {
        return true;
      }
    }
    return false;
  };

  bool returns = false;
  for (auto bb : r.blocks) {
    for (auto arg : bb->args) {
      if (used_outside(arg->uses)) {
        return reject(RegionReject::LiveOut);
      }
    }
    for (auto instr : bb->instructions) {
      r.n_instrs++;
      if (instr->is(fir::InstrType::ReturnInstr)) {
        returns = true;
      }
      if (instr->is(fir::IntrinsicSubType::VA_start) ||
          instr->is(fir::IntrinsicSubType::VA_end)) {
        return reject(RegionReject::VarArgs);
      }
      if (instr->is(fir::InstrType::AllocaInstr) &&
          !instr->args[0].is_constant()) {
        return reject(RegionReject::DynamicAlloca);
      }
      if (used_outside(instr->get_uses())) {
        return reject(RegionReject::LiveOut);
      }
      for (auto a : instr->args) {
        use(a);
      }
      for (const auto &t : instr->get_bb_args()) {
        for (auto a : t.args) {
          use(a);
        }
      }
    }
  }
  r.kind = returns ? Region::Kind::Returns : Region::Kind::NoReturn;
  if (r.n_instrs < limits.min_instrs) {
    return reject(RegionReject::TooSmall);
  }
  if (r.live_in.size() > limits.max_live_ins) {
    return reject(RegionReject::TooManyLiveIns);
  }
  // passing the address would make the alloca escape in the remaining code
  for (auto v : r.live_in) {
    auto base = decompose_ptr(v).base;
    if (base.is_instr() && base.as_instr()->is(fir::InstrType::AllocaInstr) &&
        !analyze_alloca_escape(base).escapes) {
      return reject(RegionReject::PromotableAlloca);
    }
  }
  return r;
}

IRString unique_function_name(fir::ContextData &ctx, const IRString &base) {
  if (!ctx.storage.functions.contains(base.c_str())) {
    return base;
  }
  for (u32 k = 1;; k++) {
    IRString name = base;
    name += ".";
    name += std::to_string(k);
    if (!ctx.storage.functions.contains(name.c_str())) {
      return name;
    }
  }
}

fir::Instr extract_region(fir::Function &f, const Region &r, IRString name,
                          ExtractAttribs attribs) {
  auto *ctx = f.ctx;
  const auto &fty = f.func_ty->as_func();
  const bool returns = r.kind == Region::Kind::Returns;

  IRVec<fir::TypeR> params;
  for (auto arg : r.entry->args) {
    params.push_back(arg->get_type());
  }
  const size_t n_entry_args = params.size();
  for (auto v : r.live_in) {
    params.push_back(v.get_type());
  }
  fir::TypeR void_ty = ctx->get_void_type();
  fir::FunctionTypeR new_ty =
      returns ? ctx->get_func_ty(std::span<const fir::TypeR>{fty.return_types},
                                 std::move(params))
              : ctx->get_func_ty(void_ty, std::move(params));
  auto nf = ctx->create_function(std::move(name), new_ty);
  nf->attribs.linkage = fir::Linkage::Internal;
  nf->attribs.linkvis = fir::LinkVisibility::Hidden;
  nf->attribs.no_inline = attribs.no_inline;
  nf->attribs.cold = attribs.cold;
  nf->attribs.no_return = !returns;
  nf->attribs.outlined = true;

  // copy first, the originals are removed afterwards
  fir::ContextData::V2VMap subs;
  TVec<fir::BasicBlock> new_bbs;
  for (auto bb : r.blocks) {
    new_bbs.push_back(ctx->copy(bb, subs, false, fir::MetaCopy::All, nf.func));
  }
  auto init = nf->get_entry();
  for (size_t i = 0; i < r.live_in.size(); i++) {
    subs.insert({r.live_in[i], fir::ValueR{init->args[n_entry_args + i]}});
  }
  for (auto bb : new_bbs) {
    nf->append_bbr(bb);
  }
  for (auto bb : new_bbs) {
    for (auto instr : bb->instructions) {
      instr.substitute(subs);
    }
  }
  {
    auto b = init.builder_at_end();
    auto br = b.build_branch(new_bbs[0]);
    for (size_t i = 0; i < n_entry_args; i++) {
      br.add_bb_arg(0, fir::ValueR{init->args[i]});
    }
  }

  // nothing outside uses what the region defines, the remaining uses are
  // inside the region and go away with it
  for (auto bb : r.blocks) {
    if (bb != r.entry) {
      for (auto arg : bb->args) {
        if (arg->get_n_uses() > 0) {
          arg->replace_all_uses(
              fir::ValueR{ctx->get_poisson_value(arg->get_type())});
        }
      }
    }
    for (auto instr : bb->instructions) {
      for (u32 i = 0; i < instr->n_results(); i++) {
        auto res = instr.result(i);
        if (res.get_n_uses() > 0) {
          res.replace_all_uses(
              fir::ValueR{ctx->get_poisson_value(instr.result_type(i))});
        }
      }
    }
  }
  IRVec<fir::ValueR> call_args;
  for (auto arg : r.entry->args) {
    call_args.push_back(fir::ValueR{arg});
  }
  for (auto v : r.live_in) {
    call_args.push_back(v);
  }
  for (auto bb : r.blocks) {
    while (!bb->instructions.empty()) {
      bb->remove_instr(bb->instructions.size() - 1, true);
    }
  }
  for (auto bb : r.blocks) {
    if (bb != r.entry) {
      bb->remove_from_parent(true, false, false);
    }
  }

  auto entry_bb = r.entry;
  auto b = entry_bb.builder_at_end();
  auto call = b.build_call(
      fir::ValueR{ctx->get_constant_value(nf)}, new_ty,
      std::span<const fir::TypeR>{new_ty->as_func().return_types}, call_args);
  if (!returns) {
    b.build_unreach();
  } else if (new_ty->as_func().n_results() == 0) {
    b.build_return();
  } else if (call->n_results() == 1) {
    b.build_return(call.result());
  } else {
    IRVec<fir::ValueR> rets;
    for (u32 i = 0; i < call->n_results(); i++) {
      rets.push_back(call.result(i));
    }
    b.build_return(rets);
  }
  return call;
}

}  // namespace foptim::optim
