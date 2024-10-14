#include "matcher.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/function.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "mir/func.hpp"
#include "mir/instr.hpp"
#include "mir/reg_alloc.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/analysis/live_variables.hpp"
#include "utils/logging.hpp"
#include <Tracy/tracy/Tracy.hpp>
#include <algorithm>
#include <asmjit/x86/x86operand.h>

namespace foptim::fmir {

struct Edge {
  fir::Instr from;
  fir::Instr to;
};

bool has_active_succ_edges(fir::Instr instr, FVec<Edge> &edges) {
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
  FVec<MatchTodos> match_todos;
  uint32_t n_nodes_matched = 0;

  for (size_t start_node_id = 0; start_node_id < patt.nodes.size();
       start_node_id++) {
    // iterate over all the possible initial notes since tehre can be multiple
    // candidates we need to iter over this

    // if it doesnt match check next
    if (!try_match(fir::ValueR(instr), patt.nodes[start_node_id])) {
      continue;
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

    // utils::Debug << "  Start at " << start_node_id << "\n";
    // do the actualy matching by searching "recursively" up the use chain to
    // find matches
    {
      while (!match_todos.empty()) {
        auto [child_arg, node_id] = match_todos.back();
        match_todos.pop_back();
        // TODO: can move this test to check before adding to worklist
        if (!child_arg.is_instr()) {
          return false;
        }
        if (!try_match(child_arg, patt.nodes[node_id])) {
          return false;
        }
        auto as_instr = child_arg.as_instr();
        // utils::Debug << "  Setting at " << node_id << "\n";
        // ASSERT(as_instr.is_valid());
        res.matched_instrs[node_id] = as_instr;
        n_nodes_matched++;
        for (auto edge : patt.edges) {
          if (edge.to_instr == node_id) {
            if (as_instr->args.size() > edge.to_arg) {
              match_todos.push_back(
                  {as_instr->args[edge.to_arg], edge.from_instr});
            } else {
              return false;
            }
          }
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

void find_match(fir::Instr instr, FVec<Pattern> &patts, MatchResult &res,
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

MBB apply_bb(fir::BasicBlock &bb, FVec<Edge> &active_edges,
             FVec<Pattern> &patterns, ExtraMatchData &data) {
  ZoneScopedN("Apply BB");
  MBB result_bb;

  // all the instructions in this basic block
  FVec<fir::Instr> instrs;
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
    ASSERT(false);
  } else if (type->is_ptr()) {
    return Type::Int64;
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
    auto &entry_bb = func.entry;
    for (u32 i = 0; i < entry_bb->args.size(); i++) {
      auto arg_reg = alloc.get_register(fir::ValueR{entry_bb, i});
      auto arg_type = entry_bb->args[i].type;

      res_func.args.push_back(arg_reg);
      res_func.arg_tys.push_back(convert_type(arg_type));
    }
  }

  FMap<fir::BasicBlock, u32> bbs;
  for (u32 bb_id = 0; bb_id < func.basic_blocks.size(); bb_id++) {
    bbs[func.basic_blocks[bb_id]] = bb_id;
  }

  ExtraMatchData extra_data = {alloc, bbs, lives, res_func};

  FVec<Edge> active_edges;
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

MArgument valueToArg(fir::ValueR val, FVec<MInstr> &res, DumbRegAlloc &alloc) {
  if (val.is_constant()) {
    auto consti = val.as_constant();
    if (consti->is_int()) {
      return {consti->as_int()};
    }
    if (consti->is_global()) {
      auto global = consti->as_global();
      // TODO: idk if i64 is right here

      auto helper = MArgument{alloc.get_new_register(VRegInfo{8}), Type::Int64};
      auto arg = MArgument::Mem(
          "G_" + std::to_string((u64)global.get_raw_ptr()), Type::Int64);
      res.emplace_back(Opcode::lea, helper, arg);
      return helper;
    }
  } else {
    ASSERT(val.get_type()->is_int() || val.get_type()->is_ptr());
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
      // TODO: idk if i64 is right here
      return MArgument::Mem("G_" + std::to_string((u64)global.get_raw_ptr()),
                            Type::Int64);
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

  FVec<PhiPair> pairs;
  pairs.reserve(args.args.size());
  for (size_t arg_id = 0; arg_id < args.args.size(); arg_id++) {
    auto to = valueToArg(fir::ValueR(args.bb, arg_id), res.result, data.alloc);
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
  }
  if (pairs.empty()) {
    return;
  }

  // TODO impl
  ASSERT(false);
}

constexpr FVec<Pattern> base_pats() {
  FVec<Pattern> res;
  res.reserve(100);

  using Node = Pattern::Node;
  // using Edge = Pattern::Edge;
  using InstrType = fir::InstrType;
  using NodeType = Pattern::NodeType;

  auto IntAddNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                         (u32)fir::BinaryInstrSubType::IntAdd};
  auto IntMulNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                         (u32)fir::BinaryInstrSubType::IntMul};
  auto SLTNode =
      Node{NodeType::Instr, InstrType::ICmp, (u32)fir::ICmpInstrSubType::SLT};
  auto ICMPNode = Node{NodeType::Instr, InstrType::ICmp, 0};
  auto BranchNode = Node{NodeType::Instr, InstrType::BranchInstr, 0};
  auto CondBranchNode = Node{NodeType::Instr, InstrType::CondBranchInstr, 0};
  auto ReturnNode = Node{NodeType::Instr, InstrType::ReturnInstr, 0};
  auto DirectCallNode = Node{NodeType::Instr, InstrType::DirectCallInstr, 0};
  auto StoreNode = Node{NodeType::Instr, InstrType::StoreInstr, 0};
  auto LoadNode = Node{NodeType::Instr, InstrType::LoadInstr, 0};
  auto AllocaNode = Node{NodeType::Instr, InstrType::AllocaInstr, 0};
  auto SExtNode = Node{NodeType::Instr, InstrType::SExt, 0};

  res.push_back(
      Pattern{{AllocaNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                // TODO: this should be done once for all allocas that only get
                // executed once
                auto alloca_instr = res.matched_instrs[0];
                auto rsp_reg = data.alloc.get_new_register(VRegInfo::RSP());
                auto rsp_arg = MArgument{rsp_reg, Type::Int64};

                auto res_reg = valueToArg(fir::ValueR(alloca_instr), res.result,
                                          data.alloc);

                auto size = alloca_instr->args[0].as_constant()->as_int();

                res.result.emplace_back(Opcode::sub, rsp_arg, rsp_arg, size);
                res.result.emplace_back(Opcode::mov, res_reg, rsp_arg);
                return true;
              }});
  res.push_back(Pattern{
      {IntAddNode, LoadNode},
      {{0, 1, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        ASSERT(res.matched_instrs.size() == 2);
        // utils::Debug << "MATCHED\n"
        //              << res.matched_instrs[0] << res.matched_instrs[1] <<
        //              "\n";
        ASSERT(res.matched_instrs[0].is_valid());
        ASSERT(res.matched_instrs[1].is_valid());
        // TODO("impl add+load pattern");

        // return false;
        auto add_instr = res.matched_instrs[0];
        auto load_instr = res.matched_instrs[1];

        auto res_reg =
            valueToArg(fir::ValueR(load_instr), res.result, data.alloc);
        auto res_ty = convert_type(add_instr.get_type());

        auto a0 = valueToArg(add_instr->args[0], res.result, data.alloc);

        if (add_instr->args[1].is_constant()) {
          auto c1 = add_instr->args[1].as_constant();
          if (c1->is_global()) {
            auto a1 =
                valueToArgPtr(add_instr->args[1], Type::Int64, data.alloc);
            ASSERT(a1.type == MArgument::ArgumentType::MemLabel);
            res.result.emplace_back(Opcode::mov, res_reg,
                                    MArgument::Mem(a1.label, a0.imm, res_ty));
            return true;
          }
        }

        auto a1 = valueToArg(add_instr->args[1], res.result, data.alloc);
        if (a0.isReg() && a1.isImm()) {
          res.result.emplace_back(Opcode::mov, res_reg,
                                  MArgument::Mem(a0.reg, a1.imm, res_ty));
        } else if (a0.isImm() && a1.isReg()) {
          res.result.emplace_back(Opcode::mov, res_reg,
                                  MArgument::Mem(a1.reg, a0.imm, res_ty));
        } else if (a0.isReg() && a1.isReg()) {
          res.result.emplace_back(Opcode::mov, res_reg,
                                  MArgument::Mem(a0.reg, a1.reg, res_ty));
        } else {
          // utils::Debug << "FAILED TO MATCH IT " << add_instr << "\n";
          return false;
        }
        return true;
      }});
  res.push_back(Pattern{
      {IntAddNode, StoreNode},
      {{0, 1, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        ASSERT(res.matched_instrs.size() == 2);
        // utils::Debug << "MATCHED\n"
        //              << res.matched_instrs[0] << res.matched_instrs[1] <<
        //              "\n";
        ASSERT(res.matched_instrs[0].is_valid());
        ASSERT(res.matched_instrs[1].is_valid());
        // TODO("impl add+load pattern");

        // return false;
        auto add_instr = res.matched_instrs[0];
        auto store_instr = res.matched_instrs[1];

        auto res_ty = convert_type(add_instr.get_type());

        auto a0 = valueToArg(add_instr->args[0], res.result, data.alloc);
        auto a1 = valueToArg(add_instr->args[1], res.result, data.alloc);
        auto value = valueToArg(store_instr->args[1], res.result, data.alloc);

        if (a0.isReg() && a1.isImm()) {
          res.result.emplace_back(
              Opcode::mov, MArgument::Mem(a0.reg, a1.imm, res_ty), value);
        } else if (a0.isImm() && a1.isReg()) {
          res.result.emplace_back(
              Opcode::mov, MArgument::Mem(a1.reg, a0.imm, res_ty), value);
        } else if (a0.isReg() && a1.isReg()) {
          res.result.emplace_back(
              Opcode::mov, MArgument::Mem(a0.reg, a1.reg, res_ty), value);
        } else {
          // utils::Debug << "FAILED TO MATCH IT " << add_instr << "\n";
          return false;
        }
        return true;
      }});
  res.push_back(Pattern{
      {LoadNode, IntAddNode},
      {{0, 1, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        auto load_instr = res.matched_instrs[0];
        auto add_instr = res.matched_instrs[1];
        auto res_reg =
            valueToArg(fir::ValueR(add_instr), res.result, data.alloc);
        auto a0 =
            valueToArgPtr(load_instr->args[0],
                          convert_type(load_instr.get_type()), data.alloc);
        auto a1 = valueToArg(add_instr->args[1], res.result, data.alloc);
        res.result.emplace_back(Opcode::add, res_reg, a0, a1);
        return true;
      }});
  res.push_back(
      Pattern{{LoadNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto load_instr = res.matched_instrs[0];
                auto res_reg =
                    valueToArg(fir::ValueR(load_instr), res.result, data.alloc);
                auto arg = valueToArgPtr(load_instr->args[0],
                                         convert_type(load_instr.get_type()),
                                         data.alloc);
                res.result.emplace_back(Opcode::mov, res_reg, arg);
                return true;
              }});
  res.push_back(Pattern{
      {StoreNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto store_instr = res.matched_instrs[0];
        auto value = valueToArg(store_instr->args[1], res.result, data.alloc);
        auto ptr_target =
            valueToArgPtr(store_instr->args[0],
                          convert_type(store_instr.get_type()), data.alloc);
        res.result.emplace_back(Opcode::mov, ptr_target, value);
        return true;
      }});
  res.push_back(Pattern{
      {IntAddNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto add_instr = res.matched_instrs[0];
        auto res_reg =
            valueToArg(fir::ValueR(add_instr), res.result, data.alloc);
        auto a0 = valueToArg(add_instr->args[0], res.result, data.alloc);

        auto res_ty = convert_type(add_instr.get_type());

        if (res_reg.ty != a0.ty) {
          auto res_reg =
              data.alloc.get_new_register(VRegInfo{(u8)get_size(res_ty)});
          auto helper_reg0 = MArgument(res_reg, res_ty);

          res.result.emplace_back(Opcode::mov, helper_reg0, a0);
          a0 = helper_reg0;
        }

        auto a1 = valueToArg(add_instr->args[1], res.result, data.alloc);
        if (a1.isImm()) {
          //then we gucci
        } else if (res_reg.ty != a1.ty) {
          auto res_reg =
              data.alloc.get_new_register(VRegInfo{(u8)get_size(res_ty)});
          auto helper_reg1 = MArgument(res_reg, res_ty);

          res.result.emplace_back(Opcode::mov, helper_reg1, a1);
          a1 = helper_reg1;
        }

        res.result.emplace_back(Opcode::add, res_reg, a0, a1);
        return true;
      }});
  res.push_back(
      Pattern{{IntMulNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto add_instr = res.matched_instrs[0];
                auto res_reg =
                    valueToArg(fir::ValueR(add_instr), res.result, data.alloc);

                res.result.emplace_back(
                    Opcode::mul, res_reg,
                    valueToArg(add_instr->args[0], res.result, data.alloc),
                    valueToArg(add_instr->args[1], res.result, data.alloc));
                return true;
              }});
  res.push_back(Pattern{
      {SLTNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto slt_instr = res.matched_instrs[0];
        auto res_reg = data.alloc.get_register(fir::ValueR(slt_instr));
        auto res_arg = MArgument(res_reg, convert_type(slt_instr.get_type()));
        res.result.emplace_back(
            Opcode::icmp_slt, res_arg,
            valueToArg(slt_instr->args[0], res.result, data.alloc),
            valueToArg(slt_instr->args[1], res.result, data.alloc));
        return true;
      }});
  res.push_back(
      Pattern{{BranchNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto branch_instr = res.matched_instrs[0];
                auto bb_with_args = branch_instr->bbs[0];
                auto target_bb = branch_instr->bbs[0].bb;
                ASSERT(bb_with_args.args.size() == target_bb->args.size());
                generate_bb_args(bb_with_args, res, data);
                res.result.push_back(MInstr::jmp(data.bbs[bb_with_args.bb]));
                return true;
              }});
  res.push_back(Pattern{
      {ICMPNode, CondBranchNode},
      {{0, 1, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        // utils::Debug << "WE REACHED HERE"
        //              << " +  branch\n";
        auto cmp_instr = res.matched_instrs[0];
        auto branch_instr = res.matched_instrs[1];

        if (cmp_instr->get_instr_subtype() == (u32)fir::ICmpInstrSubType::SLT) {
          auto bb_with_args = branch_instr->bbs[0];
          auto target_bb = branch_instr->bbs[0].bb;
          auto v1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
          auto v2 = valueToArg(cmp_instr->args[1], res.result, data.alloc);

          ASSERT(bb_with_args.args.size() == target_bb->args.size());
          generate_bb_args(bb_with_args, res, data);
          res.result.push_back(
              MInstr::cJmp_slt(v1, v2, data.bbs[bb_with_args.bb]));
        } else {
          utils::Debug << "Failed to smartly match cmp "
                       << cmp_instr->get_instr_subtype() << " +  branch\n";
          return false;
        }

        {
          auto bb2_with_args = branch_instr->bbs[1];
          auto target_bb2 = branch_instr->bbs[1].bb;
          ASSERT(bb2_with_args.args.size() == target_bb2->args.size());
          generate_bb_args(bb2_with_args, res, data);
          res.result.push_back(MInstr::jmp(data.bbs[bb2_with_args.bb]));
        }
        return true;
      }});
  res.push_back(Pattern{
      {CondBranchNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto branch_instr = res.matched_instrs[0];
        auto cond = valueToArg(branch_instr->args[0], res.result, data.alloc);
        {
          auto bb_with_args = branch_instr->bbs[0];
          auto target_bb = branch_instr->bbs[0].bb;
          ASSERT(bb_with_args.args.size() == target_bb->args.size());
          generate_bb_args(bb_with_args, res, data);
          res.result.push_back(MInstr::cJmp(cond, data.bbs[bb_with_args.bb]));
        }

        {
          auto bb2_with_args = branch_instr->bbs[1];
          auto target_bb2 = branch_instr->bbs[1].bb;
          ASSERT(bb2_with_args.args.size() == target_bb2->args.size());
          generate_bb_args(bb2_with_args, res, data);
          res.result.push_back(MInstr::jmp(data.bbs[bb2_with_args.bb]));
        }
        return true;
      }});
  res.push_back(Pattern{
      {ReturnNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto ret_instr = res.matched_instrs[0];
        auto ret_val = valueToArg(ret_instr->args[0], res.result, data.alloc);

        auto res_reg = data.alloc.get_new_register(fir::IRLocation{ret_instr},
                                                   ret_instr.get_type(),
                                                   VRegInfo::EAX(), data.lives);
        auto res_arg = MArgument(res_reg, convert_type(ret_instr.get_type()));

        res.result.emplace_back(Opcode::mov, res_arg, ret_val);
        res.result.emplace_back(Opcode::ret, res_arg);
        return true;
      }});
  res.push_back(Pattern{
      {SExtNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto sext_instr = res.matched_instrs[0];
        auto val = valueToArg(sext_instr->args[0], res.result, data.alloc);
        auto res_reg =
            valueToArg(fir::ValueR(sext_instr), res.result, data.alloc);

        res.result.emplace_back(Opcode::mov_sx, res_reg, val);
        return true;
      }});
  res.push_back(Pattern{
      {DirectCallNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto call_instr = res.matched_instrs[0];
        {
          // auto helper_reg = data.alloc.get_new_register(
          //     optim::IRLocation{call_instr}, fmir::Type::Int64, data.lives);
          // auto helper_arg = MArgument(helper_reg, Type::Int64);
          for (auto arg : call_instr->args) {
            auto arg_value = valueToArg(arg, res.result, data.alloc);
            res.result.emplace_back(Opcode::arg_setup, arg_value);
          }
        }

        auto res_type = call_instr.get_type();
        if (res_type->is_void() || call_instr->get_n_uses() == 0) {
          res.result.emplace_back(
              Opcode::invoke,
              MArgument(*call_instr->get_attrib("callee").try_string()));
        } else if (res_type->is_int()) {
          auto res_reg =
              valueToArg(fir::ValueR(call_instr), res.result, data.alloc);
          res.result.emplace_back(
              Opcode::invoke,
              MArgument(*call_instr->get_attrib("callee").try_string()),
              res_reg);
        } else {
          ASSERT_M(false, "impl ret value");
        }
        // {
        //   auto rsp_reg = data.alloc.get_new_register(VRegInfo::ESP());
        //   res.result.push_back(MInstr{Opcode::add,
        //                               MArgument{rsp_reg, Type::Int64},
        //                               MArgument{rsp_reg, Type::Int64},
        //                               MArgument{call_instr->args.size() *
        //                               8}});
        // }
        return true;
      }});

  return res;
}

GreedyMatcher::GreedyMatcher() : Matcher(base_pats()) {}
} // namespace foptim::fmir
