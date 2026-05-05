#include "config/compiler_config.hpp"
#include "mir/legalize_bb_form.hpp"
#include "mir/optim/bb_reordering.hpp"
#include "mir/optim/dce.hpp"
#include "mir/optim/function_pass.hpp"
#include "third_party/toml.hpp"

namespace foptim::conf::fmir {
template <typename T>
concept has_construct_function_pass_func = requires {
  {
    std::declval<T>().construct_function_pass(std::declval<typename T::Pass&>())
  } -> std::same_as<void>;
};

template <class T>
struct FunctionPassConf : public PassConfig {
  virtual std::string_view get_name() const override final { return T::Name; }
  virtual PassType pass_type() const override final {
    return PassType::MIR_Func;
  }
  virtual bool _pass_parse(void* arg) override final {
    return ((T*)this)->pass_parse(*(toml::table*)arg);
  }
  virtual ::foptim::fmir::FunctionPass* _construct_mir_func_pass() override {
    static_assert(std::is_convertible<typename T::Pass*,
                                      ::foptim::fmir::FunctionPass*>::value,
                  "The pass must inherit from module pass from public");
    static_assert(has_construct_function_pass_func<T>,
                  "When inheriting from FunctionPassConfig you gotta implement "
                  "the construct_mir_function_pass function");
    auto* alloc = utils::IRAlloc<typename T::Pass>{}.allocate(1);
    new (alloc) T::Pass{};
    ((T*)this)->construct_function_pass(*alloc);
    return static_cast<::foptim::fmir::FunctionPass*>(alloc);
  };
};

struct DCEConf : public FunctionPassConf<DCEConf> {
  static constexpr const char* Name = "DCE";
  using Pass = ::foptim::fmir::DeadCodeElim;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};

struct LegalizeBBConf : public FunctionPassConf<LegalizeBBConf> {
  static constexpr const char* Name = "LegalizeBBForm";
  using Pass = ::foptim::fmir::LegalizeBBForm;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};

struct BBReorderingConf : public FunctionPassConf<BBReorderingConf> {
  static constexpr const char* Name = "BBReordering";
  using Pass = ::foptim::fmir::BBReordering;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};

}  // namespace foptim::conf::fmir
