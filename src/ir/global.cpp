#include "global.hpp"

#include "instruction_data.hpp"

namespace foptim::fir {

bool GlobalData::verify() {
  for (const auto &use : get_uses()) {
    if (!use.user.is_valid()) {
      return false;
    }
  }
  return true;
}

}  // namespace foptim::fir

fmt::appender fmt::formatter<foptim::fir::GlobalData>::format(
    foptim::fir::GlobalData const &v, format_context &ctx) const {
  auto app = ctx.out();
  if (v.is_constant) {
    app = fmt::format_to(app, "CONSTANT");
  } else {
    app = fmt::format_to(app, "GLOBAL");
  }
  if (v.init_value == nullptr) {
    app = fmt::format_to(app, " DECL");
  }
  app = fmt::format_to(app, " {} @ {} Bytes Link: ", v.name, v.n_bytes);
  switch (v.linkage) {
    case foptim::fir::Linkage::Internal:
      app = fmt::format_to(app, "internal");
      break;
    case foptim::fir::Linkage::External:
      app = fmt::format_to(app, "external");
      break;
    case foptim::fir::Linkage::Weak:
      app = fmt::format_to(app, "weak");
      break;
    case foptim::fir::Linkage::WeakODR:
      app = fmt::format_to(app, "weakODR");
      break;
    case foptim::fir::Linkage::LinkOnce:
      app = fmt::format_to(app, "linkonce");
      break;
    case foptim::fir::Linkage::LinkOnceODR:
      app = fmt::format_to(app, "linkonceODR");
      break;
  }
  switch (v.linkvis) {
    case foptim::fir::LinkVisibility::Hidden:
      app = fmt::format_to(app, " hidden");
      break;
    case foptim::fir::LinkVisibility::Protected:
      app = fmt::format_to(app, " protected");
      break;
    case foptim::fir::LinkVisibility::Default:
  }
  if (debug) {
    for (auto r : v.reloc_info) {
      if (r.reloc_offset == 0) {
        app = fmt::format_to(app, "; @{} = {}", r.insert_offset,
                             foptim::fir::ValueR{r.ref});
      } else {
        app = fmt::format_to(app, "; @{} = {}@{} ", r.insert_offset,
                             foptim::fir::ValueR{r.ref}, r.reloc_offset);
      }
    }
    app = fmt::format_to(app, " NUSES: {}", v.get_n_uses());
  }
  return app;
}
