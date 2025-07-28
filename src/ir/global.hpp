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

  consteval explicit Global()
      : utils::SRef<std::unique_ptr<GlobalData>>(invalid()) {}

  GlobalData *operator*() { return get_raw_ptr()->get(); }

  constexpr GlobalData *operator->() {
    return utils::SRef<std::unique_ptr<GlobalData>>::operator->() -> get();
  }
  constexpr const GlobalData *operator->() const {
    return utils::SRef<std::unique_ptr<GlobalData>>::operator->() -> get();
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

  bool verify();

  GlobalData(IRString name, size_t n_bytes)
      : name(std::move(name)), n_bytes(n_bytes), reloc_info({}) {}

  GlobalData(IRString name, size_t n_bytes, uint8_t *init_value)
      : name(std::move(name)),
        n_bytes(n_bytes),
        init_value(init_value),
        reloc_info({}) {}

  // TypeR type;
  IRString name;
  size_t n_bytes;
  bool is_constant = false;
  uint8_t *init_value = nullptr;
  IRVec<RelocationInfo> reloc_info;
  Linkage linkage = Linkage::External;
  LinkVisibility linkvis = LinkVisibility::Default;
};

};  // namespace foptim::fir

template <>
class fmt::formatter<foptim::fir::GlobalData>
    : public BaseIRFormatter<foptim::fir::GlobalData> {
 public:
  appender format(foptim::fir::GlobalData const &v, format_context &ctx) const;
};

template <>
struct std::hash<foptim::fir::Global> {
  std::size_t operator()(const foptim::fir::Global &k) const {
    using foptim::u32;
    using std::hash;
    return hash<
        foptim::utils::SRef<std::unique_ptr<foptim::fir::GlobalData>>>()(k);
  }
};
