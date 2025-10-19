#pragma once
#include "optim/analysis/cfg.hpp"
#include "optim/analysis/dominators.hpp"
#include "optim/function_pass.hpp"

namespace foptim::fir {
class Function;
class BasicBlock;
}  // namespace foptim::fir
namespace foptim::optim {
class Dominators;

// + [x] remove bb with no predecessor
// + [x] merge bb with pred if thats only edge
// + [x] eliminate bb if only a single jump
// + [x] eleminate bb args if only 1 pred
// + [x] eliminate bb arg if only one unique incoming value
// + [x] eliminate bb arg if no uses
// + [x] convert conditional branch with the same target into cmove
// + [x] convert nearly duplicate bbs into single with args
// + [x] convert if else into cmove
// + [ ]

class SimplifyCFG final : public FunctionPass {
 public:
  static constexpr bool debug_tracy = false;
  static constexpr bool debug_print = false;
  enum class Res : u8 {
    // No chagne
    NoChange,
    // changed but dont need to update cfg/dom
    Changed,
    // needs update of cfg + dom
    NeedUpdate,
  };

 private:
  // if not jumped to just delete
  Res remove_dead_bb(CFG &cfg, Dominators &dom, CFG::Node &curr,
                     fir::Function &func, size_t bb_id, bool is_entry);
  // if we got a bb arg that got no use remove it
  bool remove_dead_bb_arg(CFG::Node &curr, fir::Function &func, bool is_entry);
  // if only 1 pred we can replace all the bb args with just the values of
  // the pred
  bool remove_useless_bb_args(CFG &cfg, CFG::Node &curr);
  // If we got bb args and multiple predecessors. If all incoming edges either
  // have the same value or are the bb arg itsself(a loop in which the bb arg
  // value doesnt change), then we can remove the bb arg and replace all uses
  // with the value itself
  bool remove_constant_bb_args(CFG::Node &curr, bool is_entry);
  // If we got bb args and multiple predecessors. If for 2 bb args
  // all of the incoming got the same for both
  // we can merge them both into 1 bbarg
  bool remove_dup_bb_args(CFG::Node &curr, bool is_entry);
  // if a block only has a single return/unreachable we can move the
  // return/unreachable into all previous blocks that have a single jump
  Res distribute_return_unreach(CFG &cfg, CFG::Node &curr);

  // eliminate bbs that have a unreach terminator and no way to diverge
  // prior(like for example a call)
  bool remove_unreach(CFG &cfg, CFG::Node &curr, bool is_entry);
  // if a block only contains a unconditional jump we can replace it
  // backwards(into pred) if there is no bb args or only 1 pred(secnd is handled
  // by other if)
  Res merge_empty_block_backwards(CFG &cfg, Dominators &dom, CFG::Node &curr,
                                  fir::Function &func, size_t bb_id);
  // if a block only contains a unconditional
  //  jump we can replace it forwards if were not the entry block
  bool merge_empty_block_forwards(CFG &cfg, CFG::Node &curr,
                                  fir::Function &func, size_t bb_id,
                                  bool is_entry);
  // if 1 to 1 relation between blocks we can merge them
  // TODO: this should in theory even work with multiple incmoing and then use a
  // heuristic so it can do it for any short enough block
  Res merge_linear_relation(CFG &cfg, Dominators &dom, CFG::Node &curr,
                            fir::Function &func, size_t bb_id);
  // If we got a conditionalbrach with both taking the same target
  // then we can cmove the bbargs and then do a simple branch
  bool conditional_to_cmove(CFG::Node &curr);
  // If we got 2 blocks that are identical but some constants/vars
  // we could merge them into 1 and replace differences by bb args
  // NOTE: moved to function handling it all for perofmance reasons
  bool dup_bb_to_args(fir::Function &func);
  // if we have mustprogress + an infinite loop delete it
  Res eliminate_infinite_loop(CFG &cfg, Dominators &dom, CFG::Node &curr,
                              size_t bb_id, fir::Function &func);
  // or merge 2 following conditions and combine them via && or ||
  bool merge_term_cond(CFG &cfg, CFG::Node &curr);
  // flip conditions which go to 'cold' bb so that the true branch goes to the
  // cold bb and the fallthrough is the 'hot' path
  SimplifyCFG::Res flip_cold_cond(CFG &cfg, CFG::Node &curr);

 public:
  // simplifications on the bbargs which
  Res simplify_bb_args(CFG &cfg, Dominators &dom, fir::Function &func,
                       size_t bb_id);
  Res simplify_cfg(CFG &cfg, Dominators &dom, fir::Function &func,
                   size_t bb_id);
  void apply(fir::Context & /*unused*/, fir::Function &func) override;
};

}  // namespace foptim::optim
