#pragma once
#include <utility>

#include "helpers.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/use.hpp"
#include "utils/stable_vec_ref.hpp"
#include "utils/string.hpp"
#include "utils/vec.hpp"

namespace foptim::fir {
struct GlobalData;

struct Global : public utils::SRef<std::unique_ptr<GlobalData>> {
public:
  constexpr explicit Global(utils::SRef<std::unique_ptr<GlobalData>> &&crtp) {
    this->data_ref = crtp.data_ref;
#ifdef SLOT_CHECK_GENERATION
    this->generation = crtp.generation;
#endif
  }

  GlobalData *operator*() { return get_raw_ptr()->get(); }

  constexpr GlobalData *operator->() {
    return utils::SRef<std::unique_ptr<GlobalData>>::operator->()->get();
  }
  constexpr const GlobalData *operator->() const {
    return utils::SRef<std::unique_ptr<GlobalData>>::operator->()->get();
  }
};

// TODO: should be locked used
struct GlobalData : public LockedUsed {
  struct RelocationInfo {
    size_t insert_offset;
    ConstantValueR ref;
    // used as addent in relocation
    size_t reloc_offset = 0;
  };

  GlobalData(IRString name, size_t n_bytes)
      : name(std::move(name)), n_bytes(n_bytes), reloc_info({}) {}

  GlobalData(IRString name, size_t n_bytes, uint8_t *init_value)
      : name(std::move(name)), n_bytes(n_bytes), init_value(init_value),
        reloc_info({}) {}

  // TypeR type;
  IRString name;
  size_t n_bytes;
  bool is_constant = false;
  uint8_t *init_value = nullptr;
  IRVec<RelocationInfo> reloc_info;
  Linkage linkage;
};

}; // namespace foptim::fir

template <>
class fmt::formatter<foptim::fir::GlobalData>
    : public BaseIRFormatter<foptim::fir::GlobalData> {
public:
  appender format(foptim::fir::GlobalData const &v, format_context &ctx) const {
    auto app = ctx.out();
    app = fmt::format_to(app, "GLOBAL {} @ {} Bytes Link: ", v.name, v.n_bytes);
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
    if (debug) {
      for (auto r : v.reloc_info) {
        app =
            fmt::format_to(app, " REF: {} {}", r.insert_offset, r.reloc_offset);
      }
    }
    return app;
  }
};
