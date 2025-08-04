#include "cfg.hpp"

namespace foptim::optim {

void CFG::dump() const {
  fmt::println("DUMP CFG");

  // for (const auto &node : bbrs) {
  //   fmt::println("BB: {}\n SUCC:", (void *)node.bb.get_raw_ptr());
  //   for (const auto &succ : node.succ) {
  //     fmt::println("    {}", (void *)bbrs[succ].bb.get_raw_ptr());
  //   }
  //   fmt::println(" PRED:");
  //   for (const auto &pred : node.pred) {
  //     fmt::println("    {}", (void *)bbrs[pred].bb.get_raw_ptr());
  //   }
  // }
  for (size_t node_id = 0; node_id < bbrs.size(); node_id++) {
    fmt::println("BB: {}\n SUCC:", node_id);
    for (const auto &succ : bbrs[node_id].succ) {
      fmt::println("    {}", succ);
    }
    fmt::println(" PRED:");
    for (const auto &pred : bbrs[node_id].pred) {
      fmt::println("    {}", pred);
    }
  }
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

}  // namespace foptim::optim
