#include "config/compiler_config.hpp"
#include "mir/legalize_bb_form.hpp"
#include "mir/optim/bb_reordering.hpp"
#include "mir/optim/copy_prop.hpp"
#include "mir/optim/dce.hpp"
#include "mir/optim/function_pass.hpp"
#include "mir/optim/inst_simplify.hpp"
#include "mir/optim/legalization.hpp"
#include "mir/optim/lifetime_shortening.hpp"
#include "mir/optim/lvn.hpp"
#include "mir/optim/reg_alloc.hpp"
#include "mir/optim/register_joining.hpp"
#include "mir/optim/stack_optim.hpp"
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

struct CopyPropagationConf : public FunctionPassConf<CopyPropagationConf> {
  static constexpr const char* Name = "CopyPropagation";
  using Pass = ::foptim::fmir::CopyPropagation;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};

struct LVNConf : public FunctionPassConf<LVNConf> {
  static constexpr const char* Name = "LVN";
  using Pass = ::foptim::fmir::LVN;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};

struct InstSimplifyConf : public FunctionPassConf<InstSimplifyConf> {
  static constexpr const char* Name = "InstSimplify";
  using Pass = ::foptim::fmir::InstSimplify;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};

struct InstSimplifyEarlyConf : public FunctionPassConf<InstSimplifyEarlyConf> {
  static constexpr const char* Name = "InstSimplifyEarly";
  using Pass = ::foptim::fmir::InstSimplifyEarly;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};

struct LifetimeShorteningConf
    : public FunctionPassConf<LifetimeShorteningConf> {
  static constexpr const char* Name = "LifetimeShortening";
  using Pass = ::foptim::fmir::LifetimeShortening;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};

struct LegalizerConf : public FunctionPassConf<LegalizerConf> {
  static constexpr const char* Name = "Legalizer";
  using Pass = ::foptim::fmir::Legalizer;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};

struct RegisterJoiningConf : public FunctionPassConf<RegisterJoiningConf> {
  static constexpr const char* Name = "RegisterJoining";
  using Pass = ::foptim::fmir::RegisterJoining;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};

struct RegAllocConf : public FunctionPassConf<RegAllocConf> {
  static constexpr const char* Name = "RegAlloc";
  using Pass = ::foptim::fmir::RegAlloc;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};

struct StackOptimConf : public FunctionPassConf<StackOptimConf> {
  static constexpr const char* Name = "StackOptim";
  using Pass = ::foptim::fmir::StackOptim;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};

}  // namespace foptim::conf::fmir
