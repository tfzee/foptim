#pragma once
#include <utility>

#include "ir/constant_value_ref.hpp"
// #include "types_ref.hpp"
#include "ir/use.hpp"
#include "utils/stable_vec_ref.hpp"
#include "utils/string.hpp"
#include "utils/vec.hpp"

namespace foptim::fir {
struct GlobalData;

struct Global : public utils::SRef<GlobalData> {
public:
  constexpr explicit Global(utils::SRef<GlobalData> &&crtp) {
    this->data_ref = crtp.data_ref;
#ifdef SLOT_CHECK_GENERATION
    this->generation = crtp.generation;
#endif
  }
};

struct GlobalData : public Used {
  struct RelocationInfo {
    size_t insert_offset;
    ConstantValueR ref;
    //used as addent in relocation
    size_t reloc_offset = 0;
  };

  constexpr GlobalData(IRString name, size_t n_bytes)
      : name(std::move(name)), n_bytes(n_bytes), reloc_info({}) {}

  constexpr GlobalData(IRString name, size_t n_bytes, uint8_t *init_value)
      : name(std::move(name)), n_bytes(n_bytes), init_value(init_value),
        reloc_info({}) {}

  // TypeR type;
  IRString name;
  size_t n_bytes;
  uint8_t *init_value = nullptr;
  IRVec<RelocationInfo> reloc_info;
};

}; // namespace foptim::fir

template <>
class fmt::formatter<foptim::fir::GlobalData>
    : public BaseIRFormatter<foptim::fir::GlobalData> {
public:
  appender format(foptim::fir::GlobalData const &v, format_context &ctx) const {
    auto app = ctx.out();
    app = fmt::format_to(app, "GLOBAL {} @ {} Bytes ", v.name, v.n_bytes);
    if (debug) {
      for (auto r: v.reloc_info) {
        app = fmt::format_to(app, " REF: {} {}", r.insert_offset, r.reloc_offset);
      }
    }
    return app;
  }
};
