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

void CFG::dump_graph(const char *filename) const {
  auto *file = std::fopen(filename, "w");
  fmt::println(file, "digraph G{{");
  fmt::println(file,
               "node [shape=box, fontname=\"monospace\", fontsize=10, "
               "labeljust=l];");
  for (size_t i = 0; i < bbrs.size(); i++) {
    fmt::println(file, "{} [label=\"{}\"]", i, bbrs[i].bb);
  }
  for (size_t i = 0; i < bbrs.size(); i++) {
    for (const auto &succ : bbrs[i].succ) {
      fmt::println(file, "{} -> {}", i, succ);
    }
  }
  fmt::print(file, "}}\n");
  fclose(file);
}

}  // namespace foptim::optim
