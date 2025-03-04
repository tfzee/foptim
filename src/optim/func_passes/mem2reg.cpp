#include "mem2reg.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/instruction_data.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "optim/analysis/attributer/IntRange.hpp"
#include "optim/analysis/attributer/PtrAA.hpp"
#include "optim/analysis/attributer/attributer.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "utils/logging.hpp"
#include "utils/set.hpp"
#include <algorithm>
#include <tuple>

namespace foptim::optim {

static bool can_be_converted_into_phi(fir::Instr instr) {
  for (auto usage : instr->uses) {
    if (usage.user->is(fir::InstrType::LoadInstr)) {
      continue;
    }
    if (usage.user->is(fir::InstrType::StoreInstr) && usage.argId == 0) {
      continue;
    }
    return false;
  }
  return true;
}

using AllocToPhiLoc = TMap<fir::Instr, TSet<u32>>;

// For a alloca instruction get all basic blocks it needs an phi in.
//  for this we look at all bbs that store to it
static void phi_insert_locations(fir::Function &func, fir::Instr alloca_instr,
                                 AllocToPhiLoc &res, Dominators &dom) {
  if (!alloca_instr->is(fir::InstrType::AllocaInstr)) {
    return;
  }

  if (!can_be_converted_into_phi(alloca_instr)) {
    return;
  }

  // initialize it empty so it we can use this map later to check if a
  // alloca can actually be removed or if the above condition failed
  res.insert({alloca_instr, {}});

  // for every block that contains a store we add it to the blocks to
  // consider
  TVec<u32> blocks_containing_store{};
  blocks_containing_store.reserve(func.basic_blocks.size());

  for (auto use : alloca_instr->get_uses()) {
    if (use.type == fir::UseType::NormalArg && use.argId == 0 &&
        use.user->is(fir::InstrType::StoreInstr)) {
      auto parent_bb = use.user->get_parent();
      blocks_containing_store.push_back(func.bb_id(parent_bb));
    }
  }

  TVec<u32> blocks_to_consider = blocks_containing_store;
  TSet<u32> visited;
  visited.reserve(blocks_to_consider.size());
  // now we work off that list
  // adn insert it into the previously initalized list;
  while (!blocks_to_consider.empty()) {
    u32 considering_block = blocks_to_consider.back();
    blocks_to_consider.pop_back();
    if (visited.contains(considering_block)) {
      continue;
    }
    visited.insert(considering_block);

    for (auto dommy : dom.dom_bbs[considering_block].frontier) {
      res[alloca_instr].insert(dommy);
      // if (std::find(blocks_containing_store.cbegin(),
      //               blocks_containing_store.cend(),
      //               dommy) != blocks_containing_store.end()) {
      blocks_to_consider.push_back(dommy);
      // }
    }
  }
}

static AllocToPhiLoc phi_insert_locations(fir::Function &func,
                                          Dominators &dom) {
  AllocToPhiLoc res;

  // for every alloca instruction
  for (auto basic_block : func.basic_blocks) {
    for (auto instruction : basic_block->instructions) {
      phi_insert_locations(func, instruction, res, dom);
    }
  }

  return res;
}

[[maybe_unused]]
static void
dump(const TVec<TMap<fir::ValueR, std::tuple<fir::BasicBlock, fir::ValueR>>>
         &current_variable_value) {
  (void)current_variable_value;
  TODO("Reimpl");
  // pritn << "DUMP CURR VAR VALUE\n";

  // for (size_t depthp1 = current_variable_value.size(); depthp1 > 0;
  // depthp1--) {
  //   size_t depth = depthp1 - 1;
  //   pritn << depth << "DEPTH\n";
  //   for (auto [key, val] : current_variable_value[depth]) {
  //     pritn << "key: " << key << "  val: ("
  //                  << (void *)std::get<0>(val).get_raw_ptr() << ", "
  //                  << std::get<1>(val) << ") \n";
  //   }
  // }
}

static bool decide_variable_value(
    fir::ValueR variable,
    const TVec<TMap<fir::ValueR, std::tuple<fir::BasicBlock, fir::ValueR>>>
        &current_variable_value,
    fir::ValueR &res) {
  for (size_t ip1 = current_variable_value.size(); ip1 > 0; ip1--) {
    size_t i = ip1 - 1;
    if (current_variable_value[i].contains(variable)) {
      res = std::get<1>(current_variable_value[i].at(variable));
      return true;
    }
  }
  return false;
}

using VarValueStack =
    TVec<TMap<fir::ValueR, std::tuple<fir::BasicBlock, fir::ValueR>>>;

static void decide_value_store(fir::Instr instr, size_t &i,
                               fir::BasicBlock block,
                               const AllocToPhiLoc &phi_insert_locs,
                               VarValueStack &current_variable_value) {
  if (!instr->args[0].is_instr()) {
    i++;
    return;
  }
  const auto origin_alloca = instr->args[0].as_instr();
  if (!origin_alloca->is(fir::InstrType::AllocaInstr) ||
      !phi_insert_locs.contains(origin_alloca)) {
    i++;
    return;
  }

  auto &back_ref = current_variable_value.back();
  if (back_ref.contains(instr->args[0])) {
    back_ref.at(instr->args[0]) = {block, instr->args[1]};
  } else {
    back_ref.insert({instr->args[0], {block, instr->args[1]}});
  }
  block->remove_instr(i);
}

static void decide_value_load(fir::Instr instr, size_t &i,
                              const AllocToPhiLoc &phi_insert_locs,
                              const VarValueStack &current_variable_value) {
  // auto *ctx = instr->get_parent()->get_parent().func->ctx;
  fir::ValueR load_val = fir::ValueR();

  if (!instr->args[0].is_instr()) {
    i++;
    return;
  }
  const auto origin_alloca = instr->args[0].as_instr();
  if (!origin_alloca->is(fir::InstrType::AllocaInstr) ||
      !phi_insert_locs.contains(origin_alloca)) {
    i++;
    return;
  }

  // if we cant find a value here it wasnt initialized which either makes it
  // a function argument or if it is local to the function it would be UB to
  // load it
  if (!decide_variable_value(instr->args[0], current_variable_value,
                             load_val)) {
    auto *ctx = instr->get_parent()->get_parent().func->ctx;
    load_val = fir::ValueR(ctx->get_poisson_value(instr.get_type()));
    // load_val =
    //     fir::ValueR(ctx->get_constant_value(0, instr.get_type()));
  }

  instr->replace_all_uses(load_val);
  // block->remove_instr(i);
  i++;
}

static void
decide_values_start_from(fir::Function &func, fir::BasicBlock last_bb,
                         fir::BasicBlock block, TSet<fir::BasicBlock> &visited,
                         TMap<fir::ValueR, fir::ValueR> &bb_arg_to_alloca,
                         const AllocToPhiLoc &phi_insert_locs,
                         VarValueStack &current_variable_value) {

  const auto &args = block->get_args();
  // for each bb argument we find the origin
  for (u32 arg = 0; arg < args.size(); arg++) {
    fir::ValueR var_val_res = fir::ValueR();
    // only if the argument is actually part of the allocaremoval
    if (bb_arg_to_alloca.contains(fir::ValueR(block->args[arg]))) {
      auto bb_arguemnt_value = fir::ValueR(block->args[arg]);
      auto target_alloca = bb_arg_to_alloca.at(bb_arguemnt_value);

      if (!decide_variable_value(target_alloca, current_variable_value,
                                 var_val_res)) {
        // then this is a uninit value this can be valid aslong as we dont load
        // before the next store
        auto *ctx = func.ctx;
        // TODO: should habe a uninit/poision value for these cases?
        auto *result_type =
            target_alloca.as_instr()->get_attrib("alloca::type").try_type();
        if ((*result_type)->is_float()) {
          var_val_res = fir::ValueR(ctx->get_constant_value(0.0, *result_type));
        } else {
          var_val_res = fir::ValueR(ctx->get_constant_value(0, *result_type));
        }
      }
      // then we update the arguemtns of the origin jump
      auto term = last_bb->get_terminator();

      term.replace_bb_arg(block, arg, var_val_res);
      current_variable_value.back().insert(
          {target_alloca, {block, bb_arguemnt_value}});
    }
  }
  // dump(current_variable_value);

  if (visited.contains(block)) {
    return;
  }

  visited.insert(block);

  auto &instrs = block->get_instrs();
  for (size_t i = 0; i < instrs.size();) {
    auto &instr = instrs[i];
    if (instr->is(fir::InstrType::LoadInstr)) {
      decide_value_load(instr, i, phi_insert_locs, current_variable_value);
    } else if (instr->is(fir::InstrType::StoreInstr)) {
      decide_value_store(instr, i, block, phi_insert_locs,
                         current_variable_value);
      // for (auto [key, val] :
      //      current_variable_value[current_variable_value.size() - 1]) {
      // }

    } else {
      i++;
    }
  }

  auto term = block->get_terminator();
  // TODO: idk if good idea
  for (const auto &bb_arg : term->get_bb_args()) {
    current_variable_value.emplace_back();
    decide_values_start_from(func, block, bb_arg.bb, visited, bb_arg_to_alloca,
                             phi_insert_locs, current_variable_value);
    current_variable_value.pop_back();
  }
}

void Mem2Reg::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedN("Mem2Reg");
  (void)ctx;
  CFG cfg{func};
  Dominators dom{cfg};

  // cfg.dump_graph();
  // dom.dump();
  // TODO("Fix em dominators");
  // todo verify prior no basic block args maybe?

  TMap<fir::ValueR, fir::ValueR> bb_arg_to_alloca{};
  // create all 'phis'
  auto insert_locations = phi_insert_locations(func, dom);

  for (auto &[instr, blocks] : insert_locations) {
    for (const auto &block : blocks) {
      if (instr->has_attrib("alloca::type") && instr->get_n_uses() > 0) {
        auto new_arg = ctx->storage.insert_bb_arg(
            func.basic_blocks[block],
            *instr->get_attrib("alloca::type").try_type());
        cfg.bbrs[block].bb->args.emplace_back(new_arg);
        const auto key = fir::ValueR{new_arg};
        ASSERT(!bb_arg_to_alloca.contains(key));
        bb_arg_to_alloca.insert({key, fir::ValueR(instr)});
      }
    }
  }

  TSet<fir::BasicBlock> visited{};
  TVec<TMap<fir::ValueR, std::tuple<fir::BasicBlock, fir::ValueR>>>
      current_variable_value{{}};
  decide_values_start_from(func, fir::BasicBlock(fir::BasicBlock::invalid()),
                           func.get_entry(), visited, bb_arg_to_alloca,
                           insert_locations, current_variable_value);
}
} // namespace foptim::optim
