#include "mem2reg.hpp"

#include <tuple>

#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/helper/helper.hpp"
#include "utils/set.hpp"

namespace foptim::optim {
namespace {
bool can_be_converted_into_phi(fir::Instr instr) {
  if (!instr->extra_type.is_valid()) {
    return false;
  }
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

bool type_equal_enough_for_guess(fir::TypeR got, fir::TypeR exp) {
  if (got->eql(*exp.get_raw_ptr())) {
    return true;
  }
  // int 64 and ptr are the same
  if (got->is_int() && got->as_int() == 64 && exp->is_ptr()) {
    return true;
  }
  if (exp->is_int() && exp->as_int() == 64 && got->is_ptr()) {
    return true;
  }
  return false;
}

// sometimes when converting memcpy/memset and stuff to store/load
//  it might not be able to figure out the type correctly
//  leading to i64/i32 or similar loads and stores
//  even thoug hte actual type is a floation point
//  this then prevents mem2reg from converting it into a value/bbarg
//  here we want to fix these up
//  generally when we hit a alloca and we dont know its type
//  we can check the loads/stores
//  but if we hit a load that only is used in a store and vice verse the types
//  of these can be freely changed aslong as the bitwidth matches
void fix_types(fir::Function &func) {
  TSet<fir::Instr> convertable_set;
  for (auto bb : func.basic_blocks) {
    for (auto instr : bb->instructions) {
      if (instr->is(fir::InstrType::LoadInstr) && instr->get_n_uses() == 1 &&
          instr->uses[0].user->is(fir::InstrType::StoreInstr) &&
          instr->uses[0].argId == 1) {
        convertable_set.emplace(instr);
        // convertable_set.emplace(instr->uses[0].user);
      }
    }
  }
  if (convertable_set.empty()) {
    return;
  }
  TMap<fir::Instr, fir::TypeR> convert_load_to_type;
  TVec<fir::Instr> convertable_uses;
  for (auto bb : func.basic_blocks) {
    for (auto instr : bb->instructions) {
      if (!instr->is(fir::InstrType::AllocaInstr)) {
        continue;
      }
      if (instr->extra_type.is_valid()) {
        continue;
      }
      convertable_uses.clear();
      fir::TypeR real_type;
      // fir::TypeR backup_type;
      for (auto use : instr->uses) {
        if (convertable_set.contains(use.user)) {
          convertable_uses.push_back(use.user);
        } else if (use.user->is(fir::InstrType::StoreInstr) &&
                   use.user->args[1].is_instr() &&
                   convertable_set.contains(use.user->args[1].as_instr())) {
          convertable_uses.push_back(use.user->args[1].as_instr());
        } else if (!real_type.is_valid() ||
                   type_equal_enough_for_guess(real_type,
                                               use.user->get_type())) {
          real_type = use.user.get_type();
        }
      }

      if (!real_type.is_valid()) {
        continue;
      }
      for (auto u : convertable_uses) {
        if (convert_load_to_type.contains(u)) {
          auto old_ty = convert_load_to_type.at(u);
          if (old_ty.is_valid() &&
              !type_equal_enough_for_guess(old_ty, real_type)) {
            convert_load_to_type.at(u) = fir::TypeR{};
          }
        } else {
          convert_load_to_type.insert({u, real_type});
        }
      }
    }
  }
  if (!convert_load_to_type.empty()) {
    for (auto [cu, ty] : convert_load_to_type) {
      auto store = cu->uses[0].user;
      if (!ty.is_valid() ||
          cu->get_type()->get_bitwidth() != ty->get_bitwidth() ||
          store->get_type()->get_bitwidth() != ty->get_bitwidth()) {
        continue;
      }
      const_cast<fir::InstrData *>(cu.get_raw_ptr())->value_type = ty;
      const_cast<fir::InstrData *>(cu->uses[0].user.get_raw_ptr())->value_type =
          ty;
    }
  }
}

// For a alloca instruction get all basic blocks it needs an phi in.
//  for this we look at all bbs that store to it
void phi_insert_locations(fir::Function &func, fir::Instr alloca_instr,
                          AllocToPhiLoc &res, Dominators &dom) {
  if (!alloca_instr->is(fir::InstrType::AllocaInstr)) {
    return;
  }

  // if we dont have alloca::type we can try to guess it
  if (!alloca_instr->extra_type.is_valid()) {
    fir::TypeR guessed_type{fir::TypeR::invalid()};
    for (auto usage : alloca_instr->uses) {
      bool is_load = usage.user->is(fir::InstrType::LoadInstr);
      bool is_store =
          usage.user->is(fir::InstrType::StoreInstr) && usage.argId == 0;
      // bool is_add =;
      if (is_load || is_store) {
        if (!guessed_type.is_valid() ||
            type_equal_enough_for_guess(usage.user.get_type(), guessed_type)) {
          guessed_type = usage.user.get_type();
        } else {
          guessed_type = fir::TypeR{fir::TypeR::invalid()};
          break;
        }
      } else if (usage.user->is(fir::InstrType::BinaryInstr) &&
                 usage.user->subtype == (u32)fir::BinaryInstrSubType::IntAdd &&
                 usage.argId == 0) {
        auto guess = guessType(fir::ValueR{usage.user});
        if (guess.typeless) {
          continue;
        }
        if (guess.type.is_valid()) {
          guessed_type = guess.type;
        } else {
          guessed_type = fir::TypeR{fir::TypeR::invalid()};
          break;
        }
      } else {
        // TODO: supposrt atleast add instr
        guessed_type = fir::TypeR{fir::TypeR::invalid()};
        break;
      }
    }
    if (guessed_type.is_valid()) {
      alloca_instr->extra_type = guessed_type;
    }
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

AllocToPhiLoc phi_insert_locations(fir::Function &func, Dominators &dom) {
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
void dump(
    const TVec<TMap<fir::ValueR, std::tuple<fir::BasicBlock, fir::ValueR>>>
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

bool decide_variable_value(
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

void decide_value_store(fir::Instr instr, size_t &i, fir::BasicBlock block,
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
  auto val = instr->args[1];
  if (val.is_constant() && val.as_constant()->is_int()) {
    auto cval = val.as_constant();
    if (cval->type != instr.get_type()) {
      val = fir::ValueR{block->get_parent()->ctx->get_constant_value(
          cval->as_int(), instr.get_type())};
    }
  }
  if (back_ref.contains(instr->args[0])) {
    back_ref.at(instr->args[0]) = {block, val};
  } else {
    back_ref.insert({instr->args[0], {block, val}});
  }
  block->remove_instr(i, true);
}

void decide_value_load(fir::Instr instr, size_t &i,
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
  }
  if (load_val.get_type() != instr.get_type()) {
    if (load_val.is_constant() && load_val.as_constant()->is_poison()) {
      auto *ctx = instr->get_parent()->get_parent()->ctx;
      load_val = fir::ValueR{ctx->get_poisson_value(instr.get_type())};
    } else if (load_val.get_type()->get_size() !=
                   instr.get_type()->get_size() ||
               load_val.get_type()->get_align() !=
                   instr.get_type()->get_align()) {
      fmt::println("{} != {}", instr, load_val);
      fmt::println("{} != {}", instr.get_type(), load_val.get_type());
      TODO("wrong typein alloca?");
    } else {
      fir::Builder buh{instr};
      buh.after(instr);
      load_val = fir::ValueR{buh.build_conversion_op(
          load_val, instr->get_type(), fir::ConversionSubType::BitCast)};
    }
  }

  instr->replace_all_uses(load_val);
  // block->remove_instr(i);
  i++;
}

void decide_values_start_from(fir::Function &func, fir::BasicBlock last_bb,
                              fir::BasicBlock block,
                              TSet<fir::BasicBlock> &visited,
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
        auto result_type = target_alloca.as_instr()->extra_type;
        if ((result_type)->is_float()) {
          var_val_res = fir::ValueR(ctx->get_constant_value(0.0, result_type));
        } else {
          var_val_res = fir::ValueR(ctx->get_constant_value(0, result_type));
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
}  // namespace

void Mem2Reg::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedN("Mem2Reg");

  fix_types(func);
  // fmt::println("{:cd}", func);

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
    if (instr->extra_type.is_valid() && instr->get_n_uses() > 0) {
      for (const auto &block : blocks) {
        auto new_arg = ctx->storage.insert_bb_arg(func.basic_blocks[block],
                                                  instr->extra_type);
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
}  // namespace foptim::optim
