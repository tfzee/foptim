#include "helpers.hpp"
#include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"

namespace foptim::fir {
BasicBlock insert_bb_between(BasicBlock from, BasicBlock to) {
  bool found = false;
  for (const auto &f : from->get_terminator()->bbs) {
    if (f.bb == to) {
      found = true;
      break;
    }
  }
  if (!found) {
    fmt::println("{} {}", from, to);
    ASSERT_M(false,
             "Tried to insert bb between 2 blocks but they arent neighbours");
  }
  auto bb_term = from->get_terminator();
  auto bb_indx = bb_term.get_bb_id(to);
  const auto &bb_args = bb_term->bbs[bb_indx].args;

  fir::Builder bb{from};
  auto edge_bb = bb.append_bb();
  bb.at_end(edge_bb);

  auto edge_term = bb.build_branch(to);
  for (auto old_arg : bb_args) {
    edge_term.add_bb_arg(0, old_arg);
  }

  bb_term.replace_bb(bb_indx, edge_bb, true);
  return edge_bb;
}

} // namespace foptim::fir
