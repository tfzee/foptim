#include "matcher.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "mir/matcher_helpers.hpp"
#include "mir/matcher_patterns.hpp"
#include "mir/reg_alloc.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/live_variables.hpp"
#include "utils/logging.hpp"
#include <algorithm>
#include <ranges>
#include <tracy/Tracy.hpp>

namespace foptim::fmir {

MArgument imm_to_reg(MArgument val, Type reg_type, MatchResult &res,
                     ExtraMatchData &data) {
  // ASSERT(get_size(reg_type) <= 255);

  VReg helper = data.alloc.get_new_register(reg_type);
  auto helper_arg = MArgument(helper, reg_type);
  res.result.emplace_back(Opcode::mov, helper_arg, val);
  return helper_arg;
}

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

bool try_match(fir::ValueR value, const Pattern::Node &patt) {
  if (value.is_instr() && patt.type == Pattern::NodeType::Instr) {
    auto instr = value.as_instr();
    return instr->get_instr_type() == patt.instr_type &&
           (patt.instr_subtype == 0 ||
            instr->get_instr_subtype() == patt.instr_subtype);
  }
  if (value.is_constant() && patt.type == Pattern::NodeType::Imm) {
    return true;
  }
  return false;
}

struct MatchTodos {
  fir::ValueR child;
  u32 node_id;
};

bool try_match(fir::Instr instr, const Pattern &patt, MatchResult &res,
               TVec<MatchTodos> &match_todos) {

  // for now TREE only matching
  //  for (size_t start_node_id = 0; start_node_id < patt.nodes.size();
  //       start_node_id++) {
  size_t start_node_id = patt.nodes.size() - 1;
  {
    uint32_t n_nodes_matched = 0;
    // iterate over all the possible initial notes since there can be multiple
    // candidates we need to iter over this

    // if it doesnt match check next
    if (!try_match(fir::ValueR(instr), patt.nodes[start_node_id])) {
      return false;
      // continue;
    }
    n_nodes_matched++;
    match_todos.clear();
    // if it matches setup the children inthe worklist if there is any
    {
      res.matched_instrs[start_node_id] = instr;
      for (auto edge : patt.edges) {
        if (edge.to_instr == start_node_id) {
          if (instr->args.size() > edge.to_arg) {
            match_todos.push_back({instr->args[edge.to_arg], edge.from_instr});
          } else {
            ASSERT(false);
            // return false;
          }
        }
      }
    }

    // do the actualy matching by searching "recursively" up the use chain to
    // find matches
    {
      while (!match_todos.empty()) {
        auto [child_arg, node_id] = match_todos.back();
        match_todos.pop_back();
        if (!try_match(child_arg, patt.nodes[node_id])) {
          return false;
        }
        auto as_instr = child_arg.as_instr();
        res.matched_instrs[node_id] = as_instr;
        n_nodes_matched++;
        for (auto edge : patt.edges) {
          if (edge.to_instr != node_id) {
            continue;
          }
          ASSERT(as_instr->args.size() > edge.to_arg);
          if (!as_instr->args[edge.to_arg].is_instr()) {
            return false;
          }
          match_todos.push_back({as_instr->args[edge.to_arg], edge.from_instr});
        }
      }
      // all matches worked out in the worklist then we good
      //  just need to make sure we actually matched all of the nodes
      ASSERT(n_nodes_matched <= patt.nodes.size());
      if (n_nodes_matched == patt.nodes.size()) {
        return true;
      }
    }
  }

  return false;
}

void find_match(fir::Instr instr, IRVec<Pattern> &patts, MatchResult &res,
                ExtraMatchData &data) {
  ZoneScopedN("Find Match");
  TVec<MatchTodos> match_todos;
  match_todos.reserve(5);
  for (size_t match_id = 0; match_id < patts.size(); match_id++) {
    res.result.clear();
    // if (res.matched_instrs.size() > patts[match_id].nodes.size()) {
    res.matched_instrs.resize(patts[match_id].nodes.size(),
                              fir::Instr(fir::Instr::invalid()));
    // }
    if (try_match(instr, patts[match_id], res, match_todos)) {
      if (patts[match_id].generator(res, data)) {
        res.match_id = match_id;
        return;
      }
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

MBB apply_bb(fir::BasicBlock &bb, IRVec<Pattern> &patterns,
             MatchResult &match_result, ExtraMatchData &data) {
  ZoneScopedN("Apply BB");
  MBB result_bb;
  result_bb.instrs.reserve(bb->n_instrs());

  // generate each instruction
  for (auto cur_instr : bb->instructions | std::views::reverse) {
    // fir::Instr cur_instr = worklist.front();
    // worklist.pop_front();

    // match it
    find_match(cur_instr, patterns, match_result, data);

    // insert instrs this generates
    {
      for (auto a : match_result.result | std::views::reverse) {
        result_bb.instrs.push_back(a);
      }
    }
  }

  if (bb->get_parent()->get_entry() != bb) {
    for (auto &bb_arg : bb->args) {
      auto transfer_reg =
          get_or_insert_bbarg_mapping(bb_arg, match_result, data);
      TVec<MInstr> res;
      auto real_reg = valueToArg(fir::ValueR(bb_arg), res, data.alloc);
      if (transfer_reg == real_reg) {
        continue;
      }
      for (auto minstr : res | std::views::reverse) {
        result_bb.instrs.push_back(minstr);
      }
      result_bb.instrs.emplace_back(Opcode::mov, real_reg, transfer_reg);
    }
  }

  std::reverse(result_bb.instrs.begin(), result_bb.instrs.end());
  return result_bb;
}

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
    TODO("IMPL");
  } else {
    fmt::println("{}", type);
    ASSERT(false);
  }
  std::abort();
}

MFunc GreedyMatcher::apply(fir::Function &func) {
  ZoneScopedN("Greedy Matcher");
  MFunc res_func;
  res_func.name = func.name;
  res_func.bbs.reserve(func.n_bbs());
  DumbRegAlloc alloc{};

  if (func.variadic) {
    res_func.variadic = true;
    bool found = false;
    for (auto bb : func.get_bbs()) {
      for (auto instr : bb->instructions) {
        if (instr->is(fir::InstrType::Intrinsic) &&
            instr->subtype == (u32)fir::IntrinsicSubType::VA_start) {
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

  TMap<fir::BasicBlock, u32> bbs;
  for (u32 bb_id = 0; bb_id < func.basic_blocks.size(); bb_id++) {
    bbs[func.basic_blocks[bb_id]] = bb_id;
  }

  TMap<fir::BBArgument, MArgument> bb_arg_mapping;
  ExtraMatchData extra_data = {alloc, bbs, bb_arg_mapping, res_func,
                               static_alloca_size};

  // so we dont need to realloc
  MatchResult match_result{};
  match_result.matched_instrs.reserve(5);
  match_result.result.reserve(5);

  for (auto bb : func.basic_blocks) {
    res_func.bbs.push_back(
        apply_bb(bb, this->patterns, match_result, extra_data));
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

        res.result.emplace_back(Opcode::mov, to, from);
        pairs.erase(pairs.begin() + (int64_t)pair1_id);
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
      res.result.emplace_back(Opcode::mov, save_arg, pair.from);
      pairs.push_back(PhiPair{.to = pair.to, .from = save_arg});
      found_one = true;
    }
  }

  ASSERT(pairs.empty());
}

GreedyMatcher::GreedyMatcher() : Matcher(get_pats()) {}
} // namespace foptim::fmir
