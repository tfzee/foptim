#include "cfg.hpp"

namespace foptim::optim {

void CFG::dump() const {
  TODO("reimpl");
  // print << "DUMP CFG\n";

  // for (const auto &node : bbrs) {
  //   print << "BB: " << node.bb.get_raw_ptr() << "\n  SUCC:\n";
  //   for (const auto &succ : node.succ) {
  //     print << "    " << bbrs[succ].bb.get_raw_ptr() << "\n";
  //   }
  //   print << "  PRED:\n";
  //   for (const auto &pred : node.pred) {
  //     print << "   " << bbrs[pred].bb.get_raw_ptr() << "\n";
  //   }
  // }
}
void CFG::dump_graph() const {
  TODO("reimpl");
  // print << "=================================================0\n";

  // print << "digraph {";
  // for (size_t i = 0; i < bbrs.size(); i++) {
  //   print << i << "[label=\"" << bbrs[i].bb.get_raw_ptr() << "\"] ";
  // }
  // for (size_t i = 0; i < bbrs.size(); i++) {

  //   for (const auto &succ : bbrs[i].succ) {
  //     print << i << " -> " << succ << " ";
  //   }
  // }
  // print << "}\n";
}

} // namespace foptim::optim
