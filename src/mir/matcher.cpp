#include "matcher.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "mir/matcher_patterns.hpp"
#include "mir/reg_alloc.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/live_variables.hpp"
#include "utils/logging.hpp"
#include <Tracy/tracy/Tracy.hpp>
#include <algorithm>
#include <asmjit/core/type.h>
#include <asmjit/x86/x86operand.h>

namespace foptim::fmir {

MArgument imm_to_reg(MArgument val, Type reg_type, MatchResult &res,
                     ExtraMatchData &data) {
  // ASSERT(get_size(reg_type) <= 255);

  VReg helper = data.alloc.get_new_register(VRegInfo{reg_type});
  auto helper_arg = MArgument(helper, reg_type);
  res.result.emplace_back(Opcode::mov, helper_arg, val);
  return helper_arg;
}

struct Edge {
  fir::Instr from;
  fir::Instr to;
};

using Edges = IRVec<Edge>;

bool has_active_succ_edges(fir::Instr instr, Edges &edges) {
  for (const auto &edge : edges) {
    if (edge.from == instr) {
      return true;
    }
  }
  return false;
}

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

bool try_match(fir::Instr instr, const Pattern &patt, MatchResult &res) {
  TVec<MatchTodos> match_todos;

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
    match_todos.reserve(patt.edges.size());
    // if it matches setup the children inthe worklist if there is any
    {
      res.matched_instrs[start_node_id] = instr;
      for (auto edge : patt.edges) {
        if (edge.to_instr == start_node_id) {
          if (instr->args.size() > edge.to_arg) {
            match_todos.push_back({instr->args[edge.to_arg], edge.from_instr});
          } else {
            return false;
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
        // utils::Debug << "  Setting at " << node_id << "\n";
        // ASSERT(as_instr.is_valid());
        res.matched_instrs[node_id] = as_instr;
        n_nodes_matched++;
        for (auto edge : patt.edges) {
          if (edge.to_instr != node_id) {
            continue;
          }
          if (as_instr->args.size() <= edge.to_arg) {
            return false;
          }
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
  // utils::Debug << "Trying to match instr" << instr << "\n";
  ZoneScopedN("Find Match");
  for (size_t match_id = 0; match_id < patts.size(); match_id++) {
    res.result.clear();
    res.matched_instrs.resize(patts[match_id].nodes.size(),
                              fir::Instr(fir::Instr::invalid()));
    if (try_match(instr, patts[match_id], res)) {
      // utils::Debug << "     SUCC " << match_id << "\n";
      // for (auto &matched : res.matched_instrs) {
      //   ASSERT(matched.is_valid());
      // }
      res.match_id = match_id;
      if (patts[match_id].generator(res, data)) {
        return;
      }
    }
  }

  utils::Debug << "Failed to match instr " << instr << "\n";
  ASSERT(false);
}

MBB apply_bb(fir::BasicBlock &bb, Edges &active_edges, IRVec<Pattern> &patterns,
             ExtraMatchData &data) {
  ZoneScopedN("Apply BB");
  MBB result_bb;

  // all the instructions in this basic block
  TVec<fir::Instr> instrs;
  // the edges still active in our DAG
  const size_t n_instrs = bb->n_instrs();
  // setup
  {
    instrs.reserve(n_instrs);
    for (auto instr : bb->get_instrs()) {
      instrs.push_back(instr);
      //   for (auto arg : instr->args) {
      //     if (arg.is_instr()) {
      //       active_edges.push_back({arg.as_instr(), instr});
      //     }
      //   }
    }
  }

  // to store results
  MatchResult match_result{};

  // generate each instruction
  for (size_t instr_idp1 = n_instrs; instr_idp1 > 0; instr_idp1--) {
    size_t instr_id = instr_idp1 - 1;

    // skip instructions if the result wont be used anyway unless terminator
    if (!has_active_succ_edges(instrs[instr_id], active_edges) &&
        !instrs[instr_id]->is_critical()) {
      continue;
    }

    // match it
    find_match(instrs[instr_id], patterns, match_result, data);

    // insert instrs this generates
    {
      std::reverse(match_result.result.begin(), match_result.result.end());
      result_bb.instrs.reserve(result_bb.instrs.size() +
                               match_result.result.size());
      for (auto minstr : match_result.result) {
        result_bb.instrs.push_back(minstr);
      }
    }

    // discard all edges this clears
    {
      for (size_t ip1 = active_edges.size(); ip1 > 0; ip1--) {
        const size_t i = ip1 - 1;
        const auto &active_edge = active_edges[i];

        for (const auto rem_edge : patterns[match_result.match_id].edges) {
          if (active_edge.from ==
                  match_result.matched_instrs[rem_edge.from_instr] &&
              active_edge.to ==
                  match_result.matched_instrs[rem_edge.to_instr]) {
            active_edges.erase(active_edges.begin() + i);
            break;
          }
        }
      }

      // auto rem = std::remove_if(
      //     active_edges.begin(), active_edges.end(),
      //     [&patterns, &match_result](const auto &active_edge) {
      //       for (const auto rem_edge : patterns[match_result.match_id].edges)
      //       {
      //         if (active_edge.from ==
      //                 match_result.matched_instrs[rem_edge.from_instr] &&
      //             active_edge.to ==
      //                 match_result.matched_instrs[rem_edge.to_instr]) {
      //           return true;
      //         }
      //       }
      //       return false;
      //     });
      // active_edges.erase(rem, active_edges.end());
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
    utils::Debug << type << "\n";
    ASSERT(false);
  }
  std::abort();
}

MFunc GreedyMatcher::apply(fir::Function &func) {
  ZoneScopedN("Greedy Matcher");
  MFunc res_func;
  res_func.bbs.reserve(func.n_bbs());

  optim::CFG cfg{func};
  optim::Dominators dom{cfg};
  optim::LiveVariables lives{func, cfg};
  DumbRegAlloc alloc{};
  alloc.alloc_func(func, lives);

  {
    auto entry_bb = func.get_entry();
    for (u32 i = 0; i < entry_bb->args.size(); i++) {
      auto arg_reg = alloc.get_register(fir::ValueR{entry_bb->args[i]});
      auto arg_type = entry_bb->args[i]->get_type();

      res_func.args.push_back(arg_reg);
      res_func.arg_tys.push_back(convert_type(arg_type));
    }
  }

  TMap<fir::BasicBlock, u32> bbs;
  for (u32 bb_id = 0; bb_id < func.basic_blocks.size(); bb_id++) {
    bbs[func.basic_blocks[bb_id]] = bb_id;
  }

  ExtraMatchData extra_data = {alloc, bbs, lives, res_func};

  Edges active_edges;
  active_edges.reserve(func.n_instrs() * 2);

  for (auto bb : func.basic_blocks) {
    for (auto instr : bb->get_instrs()) {
      for (auto arg : instr->args) {
        if (arg.is_instr()) {
          active_edges.push_back({arg.as_instr(), instr});
        }
      }
      for (auto &bb : instr->bbs) {
        for (auto arg : bb.args) {
          if (arg.is_instr()) {
            active_edges.push_back({arg.as_instr(), instr});
          }
        }
      }
    }
  }

  for (auto bb : func.basic_blocks) {
    res_func.bbs.push_back(
        apply_bb(bb, active_edges, this->patterns, extra_data));
  }

  res_func.name = func.name;
  res_func.clone_attribs(func);

  return res_func;
}

MArgument valueToArg(fir::ValueR val, IRVec<MInstr> &res, DumbRegAlloc &alloc) {
  if (val.is_constant()) {
    auto consti = val.as_constant();
    if (consti->is_int()) {
      switch (val.get_type()->as_int()) {
      case 8:
        return {(u8)consti->as_int()};
      case 16:
        return {(u16)consti->as_int()};
      case 32:
        return {(u32)consti->as_int()};
      case 64:
      default:
        return {consti->as_int()};
      }
    }

    if (consti->is_float()) {
      if (val.get_type()->as_float() == 32) {
        return {(f32)consti->as_float()};
      }
      return {consti->as_float()};
    }

    if (consti->is_global()) {
      auto global = consti->as_global();
      // TODO: idk if i64 is right here

      Type type_id = convert_type(val.get_type());
      auto helper =
          MArgument{alloc.get_new_register(VRegInfo{Type::Int64}), Type::Int64};
      auto arg = MArgument::Mem(
          "G_" + std::to_string((u64)global.get_raw_ptr()), type_id);
      res.emplace_back(Opcode::lea, helper, arg);
      return helper;
    }
    if (consti->is_func()) {
      auto funcy = consti->as_func();
      auto arg = MArgument::Mem(funcy->getName(), Type::Int64);
      auto helper =
          MArgument(alloc.get_new_register(VRegInfo{Type::Int64}), Type::Int64);
      res.emplace_back(Opcode::lea, helper, arg);
      return helper;
    }
  } else {
    // ASSERT(val.get_type()->is_int() || val.get_type()->is_ptr());
    Type type_id = convert_type(val.get_type());
    return {alloc.get_register(val), type_id};
  }
  ASSERT(false);
  std::abort();
}

MArgument valueToArgPtr(fir::ValueR val, Type type_id, DumbRegAlloc &alloc) {
  if (val.is_constant()) {
    auto constant = val.as_constant();
    if (constant->is_global()) {
      auto global = constant->as_global();
      Type type_id = convert_type(val.get_type());
      // TODO: idk if i64 is right here
      return MArgument::Mem("G_" + std::to_string((u64)global.get_raw_ptr()),
                            type_id);
    }
    if (constant->is_func()) {
      auto funcy = constant->as_func();
      return {funcy->getName()};
    }
  } else {
    return MArgument::Mem(alloc.get_register(val), type_id);
  }
  ASSERT(false);
  std::abort();
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
    auto to =
        valueToArg(fir::ValueR(args.bb->args[arg_id]), res.result, data.alloc);
    auto from = valueToArg(args.args[arg_id], res.result, data.alloc);
    if (to == from) {
      continue;
    }
    // utils::Debug << to << " = " << from << "\n";
    pairs.push_back({to, from});
  }

  if (pairs.empty()) {
    return;
  }

  bool found_one = true;
  while (found_one) {
    found_one = false;
    for (size_t pair1_id = 0; pair1_id < pairs.size(); pair1_id++) {
      // if there is noone writing to the same reg as the from is using then we
      // can just generate it
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

        // utils::Debug << "SIZE: " << res.result.size()
        //              << " CAP: " << res.result.capacity()
        //              << " PTR: " << res.result.data() << "\n";
        res.result.emplace_back(Opcode::mov, to, from);
        pairs.erase(pairs.begin() + (int64_t)pair1_id);
        if (pair1_id > 0) {
          pair1_id--;
        }
        found_one = true;
      }
    }

    if (!found_one && !pairs.empty()) {
      // we just save from of the pair and then generate these moves these later
      // with the saved from
      PhiPair pair = *pairs.begin();
      pairs.erase(pairs.begin() + 0);
      auto save_reg = data.alloc.get_new_register(VRegInfo{pair.from.ty});
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
