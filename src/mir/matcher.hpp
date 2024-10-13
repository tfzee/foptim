#pragma once
#include "ir/function_ref.hpp"
#include "ir/instruction_data.hpp"
#include "mir/reg_alloc.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::fmir {

struct ExtraMatchData {
  DumbRegAlloc &alloc;
  FMap<fir::BasicBlock, u32> &bbs;
  optim::LiveVariables &lives;
  MFunc &func;
};

struct MatchResult {
  // has the same order as the pattern the match originates from
  FVec<fir::Instr> matched_instrs;
  FVec<fmir::MInstr> result;
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

  FVec<Node> nodes;
  FVec<Edge> edges;

  bool (*generator)(MatchResult &, ExtraMatchData &);
};

class Matcher {
public:
  FVec<Pattern> patterns;
};

class GreedyMatcher : public Matcher {
public:
  GreedyMatcher();
  MFunc apply(fir::Function &func);
};

} // namespace foptim::fmir
