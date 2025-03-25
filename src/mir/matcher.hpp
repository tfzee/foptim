#pragma once
#include "ir/basic_block_arg.hpp"
#include "ir/function_ref.hpp"
#include "ir/instruction_data.hpp"
#include "mir/reg_alloc.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {


struct ExtraMatchData {
  DumbRegAlloc &alloc;
  TMap<fir::BasicBlock, u32> &bbs;
  TMap<fir::BBArgument, MArgument> &bb_arg_mapping;
  MFunc &func;

};

struct MatchResult {
  // has the same order as the pattern the match originates from
  TVec<fir::Instr> matched_instrs;
  TVec<fmir::MInstr> result;
  size_t match_id;
};

struct Pattern {
  enum class NodeType {
    Instr,
    Imm,
  };

  struct Node {
    NodeType type;

    fir::InstrType instr_type;
    u32 instr_subtype;
  };

  struct Edge {
    u32 from_instr;
    u32 to_instr;
    u32 to_arg;
  };

  IRVec<Node> nodes;
  IRVec<Edge> edges;

  bool (*generator)(MatchResult &, ExtraMatchData &);
};

class Matcher {
public:
  IRVec<Pattern> patterns;
};

class GreedyMatcher : public Matcher {
public:
  GreedyMatcher();
  MFunc apply(fir::Function &func);
};

MArgument valueToArgPtr(fir::ValueR val, Type type_id, DumbRegAlloc &alloc);
MArgument valueToArg(fir::ValueR val, TVec<MInstr> &res, DumbRegAlloc &alloc);
Type convert_type(fir::TypeR type);
void generate_bb_args(fir::BBRefWithArgs &args, MatchResult &res,
                      ExtraMatchData &data);
} // namespace foptim::fmir
