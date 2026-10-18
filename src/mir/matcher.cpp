#include "matcher.hpp"

#include <algorithm>
#include <ranges>

#include "config/compiler_config.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/function.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "mir/matcher_helpers.hpp"
#include "mir/matcher_patterns.hpp"
#include "mir/reg_alloc.hpp"
#include "utils/set.hpp"
#include "utils/tracy.hpp"

namespace foptim::fmir {

namespace {
// MArgument imm_to_reg(MArgument val, Type reg_type, MatchResult &res,
//                      ExtraMatchData &data) {
//   // ASSERT(get_size(reg_type) <= 255);

//   VReg helper = data.alloc.get_new_register(reg_type);
//   auto helper_arg = MArgument(helper, reg_type);
//   res.result.emplace_back(GBaseSubtype::mov, helper_arg, val);
//   return helper_arg;
// }

// struct Edge {
//   fir::Instr from;
//   fir::Instr to;
// };

// using Edges = IRVec<Edge>;

// bool has_active_succ_edges(fir::Instr instr, Edges &edges) {
//   for (const auto &edge : edges) {
//     if (edge.from == instr) {
//       return true;
//     }
//   }
//   return false;
// }

void find_match(fir::Instr instr, const RuleSet &rules, MatchResult &res,
                ExtraMatchData &data) {
  ZoneScopedN("Find Match");
  const auto subtype = instr->get_instr_subtype();
  for (const auto &rule : rules.rules_for(instr->get_instr_type())) {
    if (rule.subtype != 0 && rule.subtype != subtype) {
      continue;
    }
    res.result.clear();
    if (rule.generate(instr, res, data)) {
      return;
    }
  }

  fmt::println("Failed to match instr {}", instr);
  UNREACH();
}

// void dump_succ_edges(const Edges &active_edges, fir::BasicBlock &bb) {
//   (void)bb;
//   fmt::println("digraph {{");
//   for (auto edge : active_edges) {
//     fmt::println(R"("{:p}" -> "{:p}")", (void *)edge.from.get_raw_ptr(),
//                  (void *)edge.to.get_raw_ptr());
//   }
//   fmt::println("}}");
// }

MBB apply_bb(fir::BasicBlock &bb, const RuleSet &rules,
             MatchResult &match_result, ExtraMatchData &data) {
  ZoneScopedN("Apply BB");
  MBB result_bb;
  result_bb.instrs.reserve(bb->n_instrs() + bb->n_args());

  // generate each instruction
  for (auto cur_instr : bb->instructions | std::views::reverse) {
    // fir::Instr cur_instr = worklist.front();
    // worklist.pop_front();

    // match it
    find_match(cur_instr, rules, match_result, data);

    // insert instrs this generates
    {
      for (auto a : match_result.result | std::views::reverse) {
        result_bb.instrs.push_back(a);
      }
    }
  }

  if (bb->get_parent()->get_entry() != bb) {
    TVec<MInstr> res;
    for (auto &bb_arg : bb->args) {
      auto transfer_reg =
          get_or_insert_bbarg_mapping(bb_arg, match_result, data);
      res.clear();
      auto real_reg = valueToArg(fir::ValueR(bb_arg), res, data.alloc);
      if (transfer_reg == real_reg) {
        continue;
      }
      for (auto minstr : res | std::views::reverse) {
        result_bb.instrs.push_back(minstr);
      }
      result_bb.instrs.emplace_back(GBaseSubtype::mov, real_reg, transfer_reg);
    }
  }

  std::ranges::reverse(result_bb.instrs);
  return result_bb;
}
}  // namespace

Type convert_type(fir::TypeR type) {
  if (type->is_int()) {
    auto width = type->as_int();
    if (width <= 8) {
      return Type::Int8;
    }
    if (width <= 16) {
      return Type::Int16;
    }
    if (width <= 32) {
      return Type::Int32;
    }
    if (width <= 64) {
      return Type::Int64;
    }
    fmt::println("Int with {}", width);
    TODO("IMPL");
  } else if (type->is_ptr()) {
    return Type::Int64;
  } else if (type->is_float()) {
    auto width = type->as_float();
    if (width == 32) {
      return Type::Float32;
    }
    if (width == 64) {
      return Type::Float64;
    }
    fmt::println("float with {}", width);
    TODO("IMPL");
  } else if (type->is_vec()) {
    auto d = type->as_vec();
    switch (d.type) {
      case fir::VectorType::SubType::Integer:
        // if its a boolean width just extend so it fits with number of expected
        // values
        if (d.bitwidth == 1) {
          // if(d.member_number == 4){
          TODO("Impl");
          //   todo not sure should prob not make this legal
          // }
        }
        if (d.bitwidth == 8 && d.member_number == 16) {
          return Type::Int8x16;
        }
        if (d.bitwidth == 8 && d.member_number == 32) {
          return Type::Int8x32;
        }
        if (d.bitwidth == 16 && d.member_number == 8) {
          return Type::Int16x8;
        }
        if (d.bitwidth == 16 && d.member_number == 16) {
          return Type::Int16x16;
        }
        if (d.bitwidth == 32 && d.member_number == 4) {
          return Type::Int32x4;
        }
        if (d.bitwidth == 32 && d.member_number == 8) {
          return Type::Int32x8;
        }
        if (d.bitwidth == 64 && d.member_number == 2) {
          return Type::Int64x2;
        }
        if (d.bitwidth == 64 && d.member_number == 4) {
          return Type::Int64x4;
        }
        if (d.bitwidth == 64 && d.member_number == 8) {
          return Type::Int64x8;
        }
        fmt::println("{}", type);
        ASSERT(false);
      case fir::VectorType::SubType::Floating:
        if (d.bitwidth == 32 && d.member_number == 2) {
          return Type::Float32x2;
        }
        if (d.bitwidth == 32 && d.member_number == 4) {
          return Type::Float32x4;
        }
        if (d.bitwidth == 32 && d.member_number == 8) {
          return Type::Float32x8;
        }
        if (d.bitwidth == 32 && d.member_number == 16) {
          return Type::Float32x16;
        }
        if (d.bitwidth == 64 && d.member_number == 2) {
          return Type::Float64x2;
        }
        if (d.bitwidth == 64 && d.member_number == 4) {
          return Type::Float64x4;
        }
        if (d.bitwidth == 64 && d.member_number == 8) {
          return Type::Float64x8;
        }
        fmt::println("{}", type);
        ASSERT(false);
    }
  } else {
    fmt::println("{}", type);
    ASSERT(false);
  }
  std::abort();
}

// The moves for the arguments of the first edge of a conditional branch are
// emitted in front of the jump, so they also run on the other path. When that
// path or the condition still reads one of the parameters that are written, the
// edge gets a block of its own for the moves.
static void split_cond_branch_edges(fir::Function &func) {
  const TVec<fir::BasicBlock> blocks(func.basic_blocks.begin(),
                                     func.basic_blocks.end());
  for (auto bb : blocks) {
    auto term = bb->get_terminator();
    if (!term->is(fir::InstrType::CondBranchInstr) || term->bbs.size() != 2) {
      continue;
    }
    const auto target = term->bbs[0].bb;
    const auto other = term->bbs[1].bb;
    if (term->bbs[0].args.empty() || target == other) {
      continue;
    }

    // blocks the other path reaches before the target rewrites its parameters
    TSet<fir::BasicBlock> other_path;
    TVec<fir::BasicBlock> work = {other};
    while (!work.empty()) {
      auto curr = work.back();
      work.pop_back();
      if (curr == target || !other_path.insert(curr).second) {
        continue;
      }
      for (const auto &edge : curr->get_terminator()->bbs) {
        work.push_back(edge.bb);
      }
    }

    // the fused compare reads its operands after the moves
    fir::Instr cond_instr;
    bool has_cond_instr = false;
    if (term->args[0].is_instr()) {
      cond_instr = term->args[0].as_instr();
      has_cond_instr = true;
    }
    bool unsafe = false;
    for (auto param : target->args) {
      for (const auto &use : param->uses) {
        if ((has_cond_instr && use.user == cond_instr) ||
            other_path.contains(use.user->get_parent())) {
          unsafe = true;
        }
      }
      for (const auto &arg : term->bbs[1].args) {
        if (arg.is_bb_arg() && arg.as_bb_arg() == param) {
          unsafe = true;
        }
      }
    }
    if (unsafe) {
      fir::insert_bb_between(bb, target);
    }
  }
}

MFunc GreedyMatcher::apply(fir::Function &func, const conf::CompConf &conf) {
  ZoneScopedN("Greedy Matcher");
  split_cond_branch_edges(func);
  MFunc res_func;
  res_func.name = func.name;
  res_func.align = func.attribs.min_align;
  res_func.bbs.reserve(func.n_bbs());
  DumbRegAlloc alloc{};
  // fmt::println("{:cd}", func);

  if (func.attribs.variadic) {
    res_func.variadic = true;
    bool found = false;
    for (auto bb : func.get_bbs()) {
      for (auto instr : bb->instructions) {
        if (instr->is(fir::InstrType::Intrinsic) &&
            instr->subtype ==
                static_cast<u32>(fir::IntrinsicSubType::VA_start)) {
          found = true;
          break;
        }
      }
      if (found) {
        break;
      }
    }
    if (found) {
      res_func.needs_register_save_area = true;
    }
  }

  u32 static_alloca_size = 0;
  {
    auto entry_bb = func.get_entry();
    res_func.args.reserve(entry_bb->n_args());
    // res_func.arg_tys.reserve(entry_bb->n_args());
    for (u32 i = 0; i < entry_bb->args.size(); i++) {
      auto arg_reg = alloc.get_register(fir::ValueR{entry_bb->args[i]});
      // auto arg_type = entry_bb->args[i]->get_type();
      res_func.args.push_back(arg_reg);
      // res_func.arg_tys.push_back(convert_type(arg_type));
    }
    for (auto bb : func.basic_blocks) {
      for (auto &instr : bb->instructions) {
        if (instr->is(fir::InstrType::AllocaInstr) &&
            (bb != entry_bb || !instr->args[0].is_constant())) {
          res_func.has_dynamic_alloca = true;
        }
      }
    }
    for (auto &instr : entry_bb->instructions) {
      if (instr->is(fir::InstrType::AllocaInstr)) {
        if (instr->args[0].is_constant()) {
          auto size = instr->args[0].as_constant()->as_int();
          if (size % 16 != 0) {
            size = size + (16 - (size % 16));
          }
          static_alloca_size += size;
        }
      }
    }
  }

  res_func.static_alloca_size = static_alloca_size;

  TMap<fir::BasicBlock, u32> bbs;
  for (u32 bb_id = 0; bb_id < func.basic_blocks.size(); bb_id++) {
    bbs[func.basic_blocks[bb_id]] = bb_id;
  }

  TMap<fir::BBArgument, MArgument> bb_arg_mapping;
  ExtraMatchData extra_data = {.alloc = alloc,
                               .bbs = bbs,
                               .bb_arg_mapping = bb_arg_mapping,
                               .func = res_func,
                               .static_alloca_size = static_alloca_size,
                               .saved_sp = VReg::RSP(),
                               .config = conf};
  if (res_func.has_dynamic_alloca) {
    extra_data.saved_sp = alloc.get_new_register(Type::Int64);
  }

  // so we dont need to realloc
  MatchResult match_result{};
  match_result.result.reserve(5);

  for (auto bb : func.basic_blocks) {
    res_func.bbs.push_back(apply_bb(bb, this->rules, match_result, extra_data));
  }
  if (res_func.has_dynamic_alloca) {
    auto &entry = res_func.bbs[0].instrs;
    entry.insert(entry.begin(),
                 MInstr{GBaseSubtype::mov,
                        MArgument{extra_data.saved_sp, Type::Int64},
                        MArgument{VReg::RSP(), Type::Int64}});
  }

  // res_func.clone_attribs(func);

  return res_func;
}

struct PhiPair {
  MArgument to;
  MArgument from;
};

void generate_bb_args(fir::BBRefWithArgs &args, MatchResult &res,
                      ExtraMatchData &data) {
  if (args.args.empty()) {
    return;
  }

  TVec<PhiPair> pairs;
  pairs.reserve(args.args.size());
  for (size_t arg_id = 0; arg_id < args.args.size(); arg_id++) {
    // skip unused bb args
    if (args.bb->args[arg_id]->get_n_uses() == 0) {
      continue;
    }

    auto to = get_or_insert_bbarg_mapping(args.bb->args[arg_id], res, data);
    auto from = valueToArg(args.args[arg_id], res.result, data.alloc);
    if (to == from) {
      continue;
    }
    pairs.push_back({to, from});
  }

  if (pairs.empty()) {
    return;
  }

  bool found_one = true;
  while (found_one) {
    found_one = false;
    for (size_t pair1_id = 0; pair1_id < pairs.size(); pair1_id++) {
      // if there is noone writing to the same reg as the from is using then
      // we can just generate it
      bool collision = false;
      for (size_t pair2_id = 0; pair2_id < pairs.size(); pair2_id++) {
        auto &p1 = pairs[pair1_id];
        auto &p2 = pairs[pair2_id];
        if (p1.to.uses_same_vreg(p2.from)) {
          collision = true;
          break;
        }
      }
      if (!collision) {
        const auto to = pairs[pair1_id].to;
        const auto from = pairs[pair1_id].from;

        res.result.emplace_back(GBaseSubtype::mov, to, from);
        pairs.erase(pairs.begin() + static_cast<int64_t>(pair1_id));
        if (pair1_id > 0) {
          pair1_id--;
        }
        found_one = true;
      }
    }

    if (!found_one && !pairs.empty()) {
      // we just save from of the pair and then generate these moves these
      // later with the saved from
      PhiPair pair = *pairs.begin();
      pairs.erase(pairs.begin() + 0);
      auto save_reg = data.alloc.get_new_register(pair.from.ty);
      auto save_arg = MArgument{save_reg, pair.from.ty};
      res.result.emplace_back(GBaseSubtype::mov, save_arg, pair.from);
      pairs.push_back(PhiPair{.to = pair.to, .from = save_arg});
      found_one = true;
    }
  }

  ASSERT(pairs.empty());
}

GreedyMatcher::GreedyMatcher() : rules(builtin_rules()) {}
}  // namespace foptim::fmir
