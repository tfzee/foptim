#include "cfg.hpp"

namespace foptim::optim {

void CFG::dump() const {
  utils::Debug << "DUMP CFG\n";

  for (const auto &node : bbrs) {
    utils::Debug << "BB: " << node.bb.get_raw_ptr() << "\n  SUCC:\n";
    for (const auto &succ : node.succ) {
      utils::Debug << "    " << bbrs[succ].bb.get_raw_ptr() << "\n";
    }
    utils::Debug << "  PRED:\n";
    for (const auto &pred : node.pred) {
      utils::Debug << "   " << bbrs[pred].bb.get_raw_ptr() << "\n";
    }
  }
}
void CFG::dump_graph() const {
  utils::Debug << "=================================================0\n";

  utils::Debug << "digraph {";
  for (size_t i = 0; i < bbrs.size(); i++) {
    utils::Debug << i << "[label=\"" << bbrs[i].bb.get_raw_ptr() << "\"] ";
  }
  for (size_t i = 0; i < bbrs.size(); i++) {

    for (const auto &succ : bbrs[i].succ) {
      utils::Debug << i << " -> " << succ << " ";
    }
  }
  utils::Debug << "}\n";
}

} // namespace foptim::optim
