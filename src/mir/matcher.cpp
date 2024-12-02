#include "matcher.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value_ref.hpp"
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
      auto arg_reg = alloc.get_register(fir::ValueR{entry_bb, i});
      auto arg_type = entry_bb->args[i].type;

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

bool is_reg(fir::ValueR val) {
  if (val.is_constant()) {
    auto consti = val.as_constant();
    if (consti->is_int()) {
      return false;
    }
    if (consti->is_global()) {
      return true;
    }
  } else {
    return true;
  }
  ASSERT(false);
  std::abort();
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
    if (fir::ValueR(args.bb, arg_id).get_n_uses() == 0) {
      continue;
    }
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

constexpr auto base_pats() {
  IRVec<Pattern> res;
  res.reserve(100);

  using Node = Pattern::Node;
  // using Edge = Pattern::Edge;
  using InstrType = fir::InstrType;
  using NodeType = Pattern::NodeType;

  auto IntAddNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                         (u32)fir::BinaryInstrSubType::IntAdd};
  auto IntSubNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                         (u32)fir::BinaryInstrSubType::IntSub};
  auto IntMulNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                         (u32)fir::BinaryInstrSubType::IntMul};
  auto SRemNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                       (u32)fir::BinaryInstrSubType::IntSRem};
  auto SDivNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                       (u32)fir::BinaryInstrSubType::IntSDiv};
  auto AndNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                      (u32)fir::BinaryInstrSubType::And};
  auto OrNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                     (u32)fir::BinaryInstrSubType::Or};
  auto XorNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                      (u32)fir::BinaryInstrSubType::Xor};
  auto FloatAddNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                           (u32)fir::BinaryInstrSubType::FloatAdd};
  auto FloatSubNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                           (u32)fir::BinaryInstrSubType::FloatSub};
  auto FloatMulNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                           (u32)fir::BinaryInstrSubType::FloatMul};
  auto ShlNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                      (u32)fir::BinaryInstrSubType::Shl};
  auto ShrNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                      (u32)fir::BinaryInstrSubType::Shr};
  auto AShrNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                       (u32)fir::BinaryInstrSubType::AShr};
  // auto EQNode =
  //     Node{NodeType::Instr, InstrType::ICmp, (u32)fir::ICmpInstrSubType::EQ};
  // auto SLTNode =
  //     Node{NodeType::Instr, InstrType::ICmp,
  //     (u32)fir::ICmpInstrSubType::SLT};
  auto ICMPNode = Node{NodeType::Instr, InstrType::ICmp, 0};
  auto FCMPNode = Node{NodeType::Instr, InstrType::FCmp, 0};
  auto BranchNode = Node{NodeType::Instr, InstrType::BranchInstr, 0};
  auto CondBranchNode = Node{NodeType::Instr, InstrType::CondBranchInstr, 0};
  auto ReturnNode = Node{NodeType::Instr, InstrType::ReturnInstr, 0};
  auto DirectCallNode = Node{NodeType::Instr, InstrType::DirectCallInstr, 0};
  auto StoreNode = Node{NodeType::Instr, InstrType::StoreInstr, 0};
  auto LoadNode = Node{NodeType::Instr, InstrType::LoadInstr, 0};
  auto AllocaNode = Node{NodeType::Instr, InstrType::AllocaInstr, 0};
  auto SExtNode = Node{NodeType::Instr, InstrType::SExt, 0};
  auto ZExtNode = Node{NodeType::Instr, InstrType::ZExt, 0};
  auto ITruncNode = Node{NodeType::Instr, InstrType::ITrunc, 0};
  auto SelectNode = Node{NodeType::Instr, InstrType::SelectInstr, 0};

  res.push_back(Pattern{
      {IntAddNode, LoadNode},
      {{0, 1, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        ASSERT(res.matched_instrs.size() == 2);
        ASSERT(res.matched_instrs[0].is_valid());
        ASSERT(res.matched_instrs[1].is_valid());

        auto add_instr = res.matched_instrs[0];
        auto load_instr = res.matched_instrs[1];

        auto res_reg =
            valueToArg(fir::ValueR(load_instr), res.result, data.alloc);
        auto load_ty = convert_type(load_instr.get_type());

        auto a0 = valueToArg(add_instr->args[0], res.result, data.alloc);

        if (add_instr->args[1].is_constant()) {
          auto c1 = add_instr->args[1].as_constant();
          if (c1->is_global()) {
            auto a1 =
                valueToArgPtr(add_instr->args[1], Type::Int64, data.alloc);
            ASSERT(a1.type == MArgument::ArgumentType::MemLabel);
            res.result.emplace_back(Opcode::mov, res_reg,
                                    MArgument::Mem(a1.label, a0.imm, load_ty));
            return true;
          }
        }

        auto a1 = valueToArg(add_instr->args[1], res.result, data.alloc);
        if (a0.isReg() && a1.isImm()) {
          res.result.emplace_back(Opcode::mov, res_reg,
                                  MArgument::Mem(a0.reg, a1.imm, load_ty));
        } else if (a0.isImm() && a1.isReg()) {
          res.result.emplace_back(Opcode::mov, res_reg,
                                  MArgument::Mem(a1.reg, a0.imm, load_ty));
        } else if (a0.isReg() && a1.isReg()) {
          res.result.emplace_back(Opcode::mov, res_reg,
                                  MArgument::Mem(a0.reg, a1.reg, load_ty));
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
        //              << res.matched_instrs[0] << "\n"
        //              << res.matched_instrs[1] << "\n";
        ASSERT(res.matched_instrs[0].is_valid());
        ASSERT(res.matched_instrs[1].is_valid());
        // TODO("impl add+load pattern");

        // return false;
        auto add_instr = res.matched_instrs[0];
        auto store_instr = res.matched_instrs[1];

        auto store_ty = convert_type(store_instr.get_type());

        auto a0 = valueToArg(add_instr->args[0], res.result, data.alloc);
        auto a1 = valueToArg(add_instr->args[1], res.result, data.alloc);
        auto value = valueToArg(store_instr->args[1], res.result, data.alloc);

        if (a0.isReg() && a1.isImm()) {
          res.result.emplace_back(
              Opcode::mov, MArgument::Mem(a0.reg, a1.imm, store_ty), value);
        } else if (a0.isImm() && a1.isReg()) {
          res.result.emplace_back(
              Opcode::mov, MArgument::Mem(a1.reg, a0.imm, store_ty), value);
        } else if (a0.isReg() && a1.isReg()) {
          res.result.emplace_back(
              Opcode::mov, MArgument::Mem(a0.reg, a1.reg, store_ty), value);
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
        a0.ty = convert_type(load_instr.get_type());
        auto a1 = valueToArg(add_instr->args[1], res.result, data.alloc);
        res.result.emplace_back(Opcode::add, res_reg, a0, a1);
        return true;
      }});
  res.push_back(Pattern{
      {IntMulNode, IntAddNode},
      {{0, 1, 1}},
      [](MatchResult &res, ExtraMatchData &data) {
        auto mul_instr = res.matched_instrs[0];
        auto add_instr = res.matched_instrs[1];
        auto res_ty = convert_type(add_instr.get_type());
        auto res_ty_size = get_size(res_ty);
        if (res_ty_size != 8 && res_ty_size != 4 && res_ty_size != 2) {
          return false;
        }
        if (!is_reg(add_instr->args[0]) || !is_reg(mul_instr->args[0])) {
          return false;
        }
        if (!mul_instr->args[1].is_constant()) {
          return false;
        }
        auto consti = mul_instr->args[1].as_constant();
        if (!consti->is_int()) {
          return false;
        }
        auto consti_val = consti->as_int();

        switch (consti_val) {
        default: {
          return false;
        }
        case 1:
          consti_val = 0;
          break;
        case 2:
          consti_val = 1;
          break;
        case 4:
          consti_val = 2;
          break;
        case 8:
          consti_val = 3;
          break;
        }

        // $1 = $0 * C
        // R = $2 + $1
        // where $0 and $2 must be regs and C in [1,2,4,8]
        auto res_reg =
            valueToArg(fir::ValueR(add_instr), res.result, data.alloc);
        auto base = valueToArg(add_instr->args[0], res.result, data.alloc);
        auto indx = valueToArg(mul_instr->args[0], res.result, data.alloc);
        ASSERT(base.isReg());
        ASSERT(indx.isReg());
        res.result.emplace_back(
            Opcode::lea, res_reg,
            MArgument::Mem(base.reg, indx.reg, consti_val, res_ty));
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

        auto sub_type = (fir::ICmpInstrSubType)cmp_instr->get_instr_subtype();

        // first we check if we can output a simplified version
        if (sub_type != fir::ICmpInstrSubType::SLT &&
            sub_type != fir::ICmpInstrSubType::SGE &&
            sub_type != fir::ICmpInstrSubType::SLE &&
            sub_type != fir::ICmpInstrSubType::SGT &&
            sub_type != fir::ICmpInstrSubType::NE &&
            sub_type != fir::ICmpInstrSubType::EQ &&
            sub_type != fir::ICmpInstrSubType::ULT &&
            sub_type != fir::ICmpInstrSubType::ULE &&
            sub_type != fir::ICmpInstrSubType::UGT &&
            sub_type != fir::ICmpInstrSubType::UGE) {
          utils::Debug << "Failed to smartly match cmp "
                       << cmp_instr->get_instr_subtype() << " +  branch\n";
          return false;
        }

        if (!branch_instr->bbs[0].args.empty() ||
            !branch_instr->bbs[1].args.empty()) {
          utils::Debug << "Failed to smartly match cmp "
                       << cmp_instr->get_instr_subtype()
                       << " + branch because non emtpy bb args\n";
          return false;
        }

        auto bb_with_args = branch_instr->bbs[0];
        auto target_bb = branch_instr->bbs[0].bb;
        auto v1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
        auto v2 = valueToArg(cmp_instr->args[1], res.result, data.alloc);

        // auto comp_ty = v1_orig.ty;

        // auto v1_reg = data.alloc.get_new_register(VRegInfo{comp_ty});
        // auto v2_reg = data.alloc.get_new_register(VRegInfo{comp_ty});
        // auto v1 = MArgument{v1_reg, comp_ty};
        // auto v2 = MArgument{v2_reg, comp_ty};

        // TODO: this is a issue with lifetimes
        // res.result.emplace_back(Opcode::mov, v1, v1_orig);
        // res.result.emplace_back(Opcode::mov, v2, v2_orig);

        ASSERT(bb_with_args.args.size() == target_bb->args.size());
        ASSERT(bb_with_args.args.size() == 0);
        // generate_bb_args(bb_with_args, res, data);

        if (sub_type == fir::ICmpInstrSubType::SLT) {
          res.result.push_back(
              MInstr::cJmp_slt(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::SGT) {
          res.result.push_back(
              MInstr::cJmp_sgt(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::ULT) {
          res.result.push_back(
              MInstr::cJmp_ult(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::ULE) {
          res.result.push_back(
              MInstr::cJmp_ule(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::UGT) {
          res.result.push_back(
              MInstr::cJmp_ugt(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::UGE) {
          res.result.push_back(
              MInstr::cJmp_uge(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::SGE) {
          res.result.push_back(
              MInstr::cJmp_sge(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::SLE) {
          res.result.push_back(
              MInstr::cJmp_sle(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::EQ) {
          res.result.push_back(
              MInstr::cJmp_eq(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::NE) {
          res.result.push_back(
              MInstr::cJmp_ne(v1, v2, data.bbs[bb_with_args.bb]));
        } else {
          TODO("UNREACH");
        }

        {
          auto bb2_with_args = branch_instr->bbs[1];
          auto target_bb2 = branch_instr->bbs[1].bb;
          ASSERT(bb2_with_args.args.size() == target_bb2->args.size());
          ASSERT(bb2_with_args.args.size() == 0);
          // generate_bb_args(bb2_with_args, res, data);
          res.result.push_back(MInstr::jmp(data.bbs[bb2_with_args.bb]));
        }
        return true;
      }});
  res.push_back(Pattern{
      {FCMPNode, CondBranchNode},
      {{0, 1, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        // utils::Debug << "WE REACHED HERE"
        //              << " +  branch\n";
        auto cmp_instr = res.matched_instrs[0];
        auto branch_instr = res.matched_instrs[1];

        auto sub_type = (fir::FCmpInstrSubType)cmp_instr->get_instr_subtype();

        auto bb_with_args = branch_instr->bbs[0];
        auto target_bb = branch_instr->bbs[0].bb;
        auto v1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
        auto v2 = valueToArg(cmp_instr->args[1], res.result, data.alloc);

        ASSERT(bb_with_args.args.size() == target_bb->args.size());
        generate_bb_args(bb_with_args, res, data);

        res.result.push_back(
            MInstr::cJmp_flt(v1, v2, data.bbs[bb_with_args.bb], sub_type));

        {
          auto bb2_with_args = branch_instr->bbs[1];
          auto target_bb2 = branch_instr->bbs[1].bb;
          ASSERT(bb2_with_args.args.size() == target_bb2->args.size());
          generate_bb_args(bb2_with_args, res, data);
          res.result.push_back(MInstr::jmp(data.bbs[bb2_with_args.bb]));
        }
        return true;
      }});
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
  res.push_back(
      Pattern{{LoadNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto load_instr = res.matched_instrs[0];
                auto res_reg =
                    valueToArg(fir::ValueR(load_instr), res.result, data.alloc);
                auto arg = valueToArgPtr(load_instr->args[0],
                                         convert_type(load_instr.get_type()),
                                         data.alloc);
                arg.ty = convert_type(load_instr.get_type());
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
        ptr_target.ty = convert_type(store_instr.get_type());
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
          auto res_reg = data.alloc.get_new_register(VRegInfo{res_ty});
          auto helper_reg0 = MArgument(res_reg, res_ty);

          res.result.emplace_back(Opcode::mov, helper_reg0, a0);
          a0 = helper_reg0;
        }

        auto a1 = valueToArg(add_instr->args[1], res.result, data.alloc);
        if (a1.isImm()) {
          // then we gucci
        } else if (res_reg.ty != a1.ty) {
          auto res_reg = data.alloc.get_new_register(VRegInfo{res_ty});
          auto helper_reg1 = MArgument(res_reg, res_ty);

          res.result.emplace_back(Opcode::mov, helper_reg1, a1);
          a1 = helper_reg1;
        }

        res.result.emplace_back(Opcode::add, res_reg, a0, a1);
        return true;
      }});
  res.push_back(Pattern{
      {SelectNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto select_instr = res.matched_instrs[0];
        auto res_reg =
            valueToArg(fir::ValueR(select_instr), res.result, data.alloc);
        auto cond = valueToArg(select_instr->args[0], res.result, data.alloc);
        auto a = valueToArg(select_instr->args[1], res.result, data.alloc);
        auto b = valueToArg(select_instr->args[2], res.result, data.alloc);

        res.result.emplace_back(Opcode::mov, res_reg, a);
        res.result.emplace_back(Opcode::cmov, res_reg, cond, b);
        return true;
      }});
  res.push_back(
      Pattern{{IntSubNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto sub_instr = res.matched_instrs[0];
                auto res_reg =
                    valueToArg(fir::ValueR(sub_instr), res.result, data.alloc);

                res.result.emplace_back(
                    Opcode::sub, res_reg,
                    valueToArg(sub_instr->args[0], res.result, data.alloc),
                    valueToArg(sub_instr->args[1], res.result, data.alloc));
                return true;
              }});
  res.push_back(Pattern{
      {ShlNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto shift_instr = res.matched_instrs[0];
        auto res_reg =
            valueToArg(fir::ValueR(shift_instr), res.result, data.alloc);

        auto a = valueToArg(shift_instr->args[0], res.result, data.alloc);
        auto b = valueToArg(shift_instr->args[1], res.result, data.alloc);

        if (b.isImm()) {
          res.result.emplace_back(Opcode::shl, res_reg, a, b);
        } else {
          auto shift_reg =
              data.alloc.get_new_pinned_register({shift_instr}, VRegInfo::CL());
          auto shift_reg_arg = MArgument(shift_reg, Type::Int8);
          if (b.ty == Type::Int8) {
            res.result.emplace_back(Opcode::mov, shift_reg_arg, b);
          } else {
            res.result.emplace_back(Opcode::itrunc, shift_reg_arg, b);
          }
          res.result.emplace_back(Opcode::shl, res_reg, a, shift_reg_arg);
        }
        return true;
      }});
  res.push_back(Pattern{
      {ShrNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto shift_instr = res.matched_instrs[0];
        auto res_reg =
            valueToArg(fir::ValueR(shift_instr), res.result, data.alloc);

        auto a = valueToArg(shift_instr->args[0], res.result, data.alloc);
        auto b = valueToArg(shift_instr->args[1], res.result, data.alloc);

        if (b.isImm()) {
          res.result.emplace_back(Opcode::shr, res_reg, a, b);
        } else {
          auto shift_reg =
              data.alloc.get_new_pinned_register({shift_instr}, VRegInfo::CL());
          auto shift_reg_arg = MArgument(shift_reg, Type::Int8);
          if (b.ty == Type::Int8) {
            res.result.emplace_back(Opcode::mov, shift_reg_arg, b);
          } else {
            res.result.emplace_back(Opcode::itrunc, shift_reg_arg, b);
          }
          res.result.emplace_back(Opcode::shr, res_reg, a, shift_reg_arg);
        }
        return true;
      }});
  res.push_back(Pattern{
      {AShrNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto shift_instr = res.matched_instrs[0];
        auto res_reg =
            valueToArg(fir::ValueR(shift_instr), res.result, data.alloc);

        auto a = valueToArg(shift_instr->args[0], res.result, data.alloc);
        auto b = valueToArg(shift_instr->args[1], res.result, data.alloc);

        if (b.isImm()) {
          res.result.emplace_back(Opcode::sar, res_reg, a, b);
        } else {
          auto shift_reg =
              data.alloc.get_new_pinned_register({shift_instr}, VRegInfo::CL());
          auto shift_reg_arg = MArgument(shift_reg, Type::Int8);
          if (b.ty == Type::Int8) {
            res.result.emplace_back(Opcode::mov, shift_reg_arg, b);
          } else {
            res.result.emplace_back(Opcode::itrunc, shift_reg_arg, b);
          }
          res.result.emplace_back(Opcode::sar, res_reg, a, shift_reg_arg);
        }
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
  res.push_back(
      Pattern{{OrNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto or_instr = res.matched_instrs[0];
                auto res_reg =
                    valueToArg(fir::ValueR(or_instr), res.result, data.alloc);

                res.result.emplace_back(
                    Opcode::lor, res_reg,
                    valueToArg(or_instr->args[0], res.result, data.alloc),
                    valueToArg(or_instr->args[1], res.result, data.alloc));
                return true;
              }});
  res.push_back(
      Pattern{{AndNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto and_instr = res.matched_instrs[0];
                auto res_reg =
                    valueToArg(fir::ValueR(and_instr), res.result, data.alloc);

                res.result.emplace_back(
                    Opcode::land, res_reg,
                    valueToArg(and_instr->args[0], res.result, data.alloc),
                    valueToArg(and_instr->args[1], res.result, data.alloc));
                return true;
              }});
  res.push_back(
      Pattern{{XorNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto xor_instr = res.matched_instrs[0];
                auto res_reg =
                    valueToArg(fir::ValueR(xor_instr), res.result, data.alloc);

                res.result.emplace_back(
                    Opcode::lxor, res_reg,
                    valueToArg(xor_instr->args[0], res.result, data.alloc),
                    valueToArg(xor_instr->args[1], res.result, data.alloc));
                return true;
              }});
  res.push_back(Pattern{
      {SRemNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto srem_instr = res.matched_instrs[0];
        // FIXME: variable size
        auto res_div = data.alloc.get_new_register(fir::IRLocation{srem_instr},
                                                   srem_instr.get_type(),
                                                   VRegInfo::EAX(), data.lives);
        auto res_rem = data.alloc.get_new_register(fir::IRLocation{srem_instr},
                                                   srem_instr.get_type(),
                                                   VRegInfo::EDX(), data.lives);
        auto res_reg =
            valueToArg(fir::ValueR(srem_instr), res.result, data.alloc);
        auto res_div_arg =
            MArgument(res_div, convert_type(srem_instr.get_type()));
        auto res_rem_arg =
            MArgument(res_rem, convert_type(srem_instr.get_type()));

        res.result.emplace_back(
            Opcode::idiv, res_div_arg, res_rem_arg,
            valueToArg(srem_instr->args[0], res.result, data.alloc),
            valueToArg(srem_instr->args[1], res.result, data.alloc));
        res.result.emplace_back(Opcode::mov, res_reg, res_rem_arg);
        return true;
      }});
  res.push_back(Pattern{
      {SDivNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto sdiv_instr = res.matched_instrs[0];
        // FIXME: variable size
        auto res_div = data.alloc.get_new_register(fir::IRLocation{sdiv_instr},
                                                   sdiv_instr.get_type(),
                                                   VRegInfo::EAX(), data.lives);
        auto res_rem = data.alloc.get_new_register(fir::IRLocation{sdiv_instr},
                                                   sdiv_instr.get_type(),
                                                   VRegInfo::EDX(), data.lives);
        auto res_reg =
            valueToArg(fir::ValueR(sdiv_instr), res.result, data.alloc);
        auto res_div_arg =
            MArgument(res_div, convert_type(sdiv_instr.get_type()));
        auto res_rem_arg =
            MArgument(res_rem, convert_type(sdiv_instr.get_type()));

        res.result.emplace_back(
            Opcode::idiv, res_div_arg, res_rem_arg,
            valueToArg(sdiv_instr->args[0], res.result, data.alloc),
            valueToArg(sdiv_instr->args[1], res.result, data.alloc));
        res.result.emplace_back(Opcode::mov, res_reg, res_div_arg);
        return true;
      }});
  res.push_back(Pattern{
      {ICMPNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto cmp_instr = res.matched_instrs[0];
        auto res_reg = data.alloc.get_register(fir::ValueR(cmp_instr));

        auto res_arg = MArgument(res_reg, convert_type(cmp_instr.get_type()));

        auto arg1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
        auto arg2 = valueToArg(cmp_instr->args[1], res.result, data.alloc);

        Opcode op = Opcode::icmp_eq;

        switch ((fir::ICmpInstrSubType)cmp_instr->get_instr_subtype()) {
        case fir::ICmpInstrSubType::SLT:
          op = Opcode::icmp_slt;
          break;
        case fir::ICmpInstrSubType::ULT:
          op = Opcode::icmp_ult;
          break;
        case fir::ICmpInstrSubType::NE:
          op = Opcode::icmp_ne;
          break;
        case fir::ICmpInstrSubType::EQ:
          op = Opcode::icmp_eq;
          break;
        case fir::ICmpInstrSubType::SGT:
          op = Opcode::icmp_sgt;
          break;
        case fir::ICmpInstrSubType::UGT:
          op = Opcode::icmp_ugt;
          break;
        case fir::ICmpInstrSubType::UGE:
          op = Opcode::icmp_uge;
          break;
        case fir::ICmpInstrSubType::ULE:
          op = Opcode::icmp_ule;
          break;
        case fir::ICmpInstrSubType::SGE:
          op = Opcode::icmp_sge;
          break;
        case fir::ICmpInstrSubType::SLE:
          op = Opcode::icmp_sle;
          break;
        case fir::ICmpInstrSubType::INVALID:
          TODO("UNREACH");
        }
        res.result.emplace_back(op, res_arg, arg1, arg2);
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
        if (ret_instr->has_args()) {
          auto ret_val = valueToArg(ret_instr->args[0], res.result, data.alloc);
          auto is_float_val = ret_val.ty == fmir::Type::Float64 ||
                              ret_val.ty == fmir::Type::Float32;

          if (!is_float_val &&
              (!ret_val.isReg() || ret_val.reg.info.ty != VRegType::A)) {
            auto converted_type = convert_type(ret_instr.get_type());
            auto res_reg = data.alloc.get_new_register(
                fir::IRLocation{ret_instr}, ret_instr.get_type(),
                VRegInfo{VRegType::A, converted_type}, data.lives);
            auto res_arg = MArgument(res_reg, converted_type);
            res.result.emplace_back(Opcode::mov, res_arg, ret_val);
            res.result.emplace_back(Opcode::ret, res_arg);
          } else if (is_float_val && (!ret_val.isReg() ||
                                      ret_val.reg.info.ty != VRegType::mm0)) {
            auto converted_type = convert_type(ret_instr.get_type());
            auto res_reg = data.alloc.get_new_register(
                fir::IRLocation{ret_instr}, ret_instr.get_type(),
                VRegInfo{VRegType::mm0, converted_type}, data.lives);
            auto res_arg = MArgument(res_reg, converted_type);
            utils::Debug << "RETTY\n";
            res.result.emplace_back(Opcode::mov, res_arg, ret_val);
            res.result.emplace_back(Opcode::ret, res_arg);
          } else {
            res.result.emplace_back(Opcode::ret, ret_val);
          }
        } else {
          res.result.emplace_back(Opcode::ret);
        }
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
      {ZExtNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto zext_instr = res.matched_instrs[0];
        auto val = valueToArg(zext_instr->args[0], res.result, data.alloc);
        auto res_reg =
            valueToArg(fir::ValueR(zext_instr), res.result, data.alloc);

        res.result.emplace_back(Opcode::mov_zx, res_reg, val);
        return true;
      }});
  res.push_back(Pattern{
      {ITruncNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto itrunc_instr = res.matched_instrs[0];
        auto val = valueToArg(itrunc_instr->args[0], res.result, data.alloc);
        auto res_reg =
            valueToArg(fir::ValueR(itrunc_instr), res.result, data.alloc);
        res.result.emplace_back(Opcode::itrunc, res_reg, val);
        return true;
      }});
  res.push_back(Pattern{
      {DirectCallNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto call_instr = res.matched_instrs[0];
        {
          // auto helper_reg = data.alloc.get_new_register(
          //     optim::IRLocation{call_instr}, fmir::Type::Int64,
          //     data.lives);
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
        } else if (res_type->is_float()) {
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
  res.push_back(Pattern{
      {FloatAddNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto f_add_instr = res.matched_instrs[0];
        auto a1 = valueToArg(f_add_instr->args[0], res.result, data.alloc);
        auto a2 = valueToArg(f_add_instr->args[1], res.result, data.alloc);
        auto res_reg =
            valueToArg(fir::ValueR(f_add_instr), res.result, data.alloc);

        res.result.emplace_back(Opcode::fadd, res_reg, a1, a2);
        return true;
      }});
  res.push_back(Pattern{
      {FloatSubNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto f_sub_instr = res.matched_instrs[0];
        auto a1 = valueToArg(f_sub_instr->args[0], res.result, data.alloc);
        auto a2 = valueToArg(f_sub_instr->args[1], res.result, data.alloc);
        auto res_reg =
            valueToArg(fir::ValueR(f_sub_instr), res.result, data.alloc);

        res.result.emplace_back(Opcode::fsub, res_reg, a1, a2);
        return true;
      }});
  res.push_back(Pattern{
      {FloatMulNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto f_mul_instr = res.matched_instrs[0];
        auto a1 = valueToArg(f_mul_instr->args[0], res.result, data.alloc);
        auto a2 = valueToArg(f_mul_instr->args[1], res.result, data.alloc);
        auto res_reg =
            valueToArg(fir::ValueR(f_mul_instr), res.result, data.alloc);

        res.result.emplace_back(Opcode::fmul, res_reg, a1, a2);
        return true;
      }});
  return res;
}

GreedyMatcher::GreedyMatcher() : Matcher(base_pats()) {}
} // namespace foptim::fmir
