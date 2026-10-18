#pragma once
#include <array>

#include "config/compiler_config.hpp"
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
  u32 static_alloca_size;
  // only valid if the function has dynamic allocas
  VReg saved_sp;
  const conf::CompConf &config;
};

struct MatchResult {
  TVec<fmir::MInstr> result;
};

// A rule is tried on instructions of its kind (and subtype, 0 means any).
// It checks the tree below the instruction, emits the MIR into
// `MatchResult::result` and returns false if it does not apply.
struct Rule {
  using Generator = bool (*)(fir::Instr, MatchResult &, ExtraMatchData &);

  u32 subtype;
  Generator generate;
};

// Rules are bucketed by the kind of the instruction they start at. Within a
// bucket the order of registration is the priority, the first rule that
// succeeds wins.
class RuleSet {
 public:
  static constexpr size_t NKinds =
      static_cast<size_t>(fir::InstrType::Intrinsic) + 1;

  template <class OpKind>
  void add(OpKind, Rule::Generator generate) {
    by_kind_[static_cast<size_t>(OpKind::type)].push_back(
        Rule{.subtype = OpKind::subtype, .generate = generate});
  }

  [[nodiscard]] const FVec<Rule> &rules_for(fir::InstrType type) const {
    return by_kind_[static_cast<size_t>(type)];
  }

 private:
  std::array<FVec<Rule>, NKinds> by_kind_;
};

class GreedyMatcher {
 public:
  GreedyMatcher();
  MFunc apply(fir::Function &func, const conf::CompConf &config);

 private:
  const RuleSet &rules;
};

Type convert_type(fir::TypeR type);
void generate_bb_args(fir::BBRefWithArgs &args, MatchResult &res,
                      ExtraMatchData &data);
}  // namespace foptim::fmir
