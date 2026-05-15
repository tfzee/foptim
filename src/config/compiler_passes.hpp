#include <fmt/base.h>

#include <string_view>
#include <third_party/toml.hpp>

#include "compiler_config.hpp"
#include "optim/func_passes/cmp_known_val_prop.hpp"
#include "optim/func_passes/constant_loop_eval.hpp"
#include "optim/func_passes/dce.hpp"
#include "optim/func_passes/double_load.hpp"
#include "optim/func_passes/inst_simplify.hpp"
#include "optim/func_passes/intrin_simplify.hpp"
#include "optim/func_passes/legalize_struct.hpp"
#include "optim/func_passes/legalize_vecs.hpp"
#include "optim/func_passes/licm.hpp"
#include "optim/func_passes/llvm_intrin_lowering.hpp"
#include "optim/func_passes/loop_rotate.hpp"
#include "optim/func_passes/loop_simplify.hpp"
#include "optim/func_passes/loop_unroll.hpp"
#include "optim/func_passes/loop_unswitch.hpp"
#include "optim/func_passes/lvn.hpp"
#include "optim/func_passes/mem2reg.hpp"
#include "optim/func_passes/merge_alloca.hpp"
#include "optim/func_passes/print.hpp"
#include "optim/func_passes/sccp.hpp"
#include "optim/func_passes/simplify_cfg.hpp"
#include "optim/func_passes/slp_vectorizer.hpp"
#include "optim/func_passes/sora.hpp"
#include "optim/func_passes/stack_known_bits.hpp"
#include "optim/func_passes/tail_rec_elim.hpp"
#include "optim/func_passes/verify_pass.hpp"
#include "optim/function_pass.hpp"
#include "optim/module_pass.hpp"
#include "optim/module_passes/IPCP.hpp"
#include "optim/module_passes/arg_promotion.hpp"
#include "optim/module_passes/func_property_annotator.hpp"
#include "optim/module_passes/function_dedup.hpp"
#include "optim/module_passes/global_dce.hpp"
#include "optim/module_passes/global_promotion.hpp"
#include "optim/module_passes/inline.hpp"
#include "utils/arena.hpp"

namespace foptim::conf {

template <typename T>
concept has_construct_module_pass_func = requires {
  {
    std::declval<T>().construct_module_pass(std::declval<typename T::Pass &>())
  } -> std::same_as<void>;
};
template <typename T>
concept has_construct_function_pass_func = requires {
  {
    std::declval<T>().construct_function_pass(
        std::declval<typename T::Pass &>())
  } -> std::same_as<void>;
};

template <class T> struct ModulePassConf : public PassConfig {
  virtual PassConfig *clone() const override final {
    return new T(static_cast<const T &>(*this));
  }
  virtual std::string_view get_name() const override final {
    if (override_name.empty()) {
      return T::BaseName;
    } else {
      return override_name;
    }
  }
  virtual PassType pass_type() const override final {
    return PassType::FIR_Module;
  }
  virtual bool _pass_parse(void *arg) override final {
    return (static_cast<T *>(this))
        ->pass_parse(*static_cast<toml::table *>(arg));
  }
  virtual optim::ModulePass *_construct_module_pass() override {
    static_assert(
        std::is_convertible<typename T::Pass *, optim::ModulePass *>::value,
        "The pass must inherit from module pass from public");
    static_assert(has_construct_module_pass_func<T>,
                  "When inheriting from ModulePassConfig you gotta implement "
                  "the construct_module_pass function");
    auto *alloc = utils::IRAlloc<typename T::Pass>{}.allocate(1);
    new (alloc) T::Pass{};
    (static_cast<T *>(this))->construct_module_pass(*alloc);
    return static_cast<optim::ModulePass *>(alloc);
  };
};

template <class T> struct FunctionPassConf : public PassConfig {
  virtual PassConfig *clone() const override final {
    return new T(static_cast<const T &>(*this));
  }
  virtual std::string_view get_name() const override final {
    if (override_name.empty()) {
      return T::BaseName;
    } else {
      return override_name;
    }
  }
  virtual PassType pass_type() const override final {
    return PassType::FIR_Function;
  }
  virtual bool _pass_parse(void *arg) override final {
    return (static_cast<T *>(this))
        ->pass_parse(*static_cast<toml::table *>(arg));
  }
  virtual optim::FunctionPass *_construct_function_pass() override {
    static_assert(
        std::is_convertible<typename T::Pass *, optim::FunctionPass *>::value,
        "The pass must inherit from module pass from public");
    static_assert(has_construct_function_pass_func<T>,
                  "When inheriting from FunctionPassConfig you gotta implement "
                  "the construct_function_pass function");
    auto *alloc = utils::IRAlloc<typename T::Pass>{}.allocate(1);
    new (alloc) T::Pass{};
    (static_cast<T *>(this))->construct_function_pass(*alloc);
    return static_cast<optim::FunctionPass *>(alloc);
  };
};

struct PrintFuncConf : public FunctionPassConf<PrintFuncConf>,
                       optim::PrintFunc::Config {
  static constexpr const char *BaseName = "PrintFunc";
  using Pass = optim::PrintFunc;
  FString name_match;
  bool pass_parse(toml::table &tbl) {
    auto view = tbl["name_matching"].value_or<std::string_view>("");
    if (!view.empty()) {
      name_match = view;
    }
    return true;
  }

  void construct_function_pass(Pass &p) {
    p.config = *static_cast<Pass::Config *>(this);
  };
};
struct VerifyFuncConf : public FunctionPassConf<VerifyFuncConf> {
  static constexpr const char *BaseName = "VerifyFunc";
  using Pass = optim::VerifyFunc;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct DCEConf : public FunctionPassConf<DCEConf> {
  static constexpr const char *BaseName = "DCE";
  using Pass = optim::DCE;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct LegalizeStructsConf : public FunctionPassConf<LegalizeStructsConf> {
  static constexpr const char *BaseName = "LegalizeStructs";
  using Pass = optim::LegalizeStructs;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct LLVMIntrinsicLoweringConf
    : public FunctionPassConf<LLVMIntrinsicLoweringConf> {
  static constexpr const char *BaseName = "LLVMIntrinsicLowering";
  using Pass = optim::LLVMInstrinsicLowering;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct SORAConf : public FunctionPassConf<SORAConf> {
  static constexpr const char *BaseName = "SORA";
  using Pass = optim::SORA;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct Mem2RegConf : public FunctionPassConf<Mem2RegConf> {
  static constexpr const char *BaseName = "Mem2Reg";
  using Pass = optim::Mem2Reg;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct DoubleLoadElimConf : public FunctionPassConf<DoubleLoadElimConf> {
  static constexpr const char *BaseName = "DoubleLoadElim";
  using Pass = optim::DoubleLoadElim;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct IntrinSimplifyConf : public FunctionPassConf<IntrinSimplifyConf> {
  static constexpr const char *BaseName = "IntrinSimplify";
  using Pass = optim::IntrinSimplify;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct InstSimplifyConf : public FunctionPassConf<InstSimplifyConf> {
  static constexpr const char *BaseName = "InstSimplify";
  using Pass = optim::InstSimplify;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct SimplifyCFGConf : public FunctionPassConf<SimplifyCFGConf> {
  static constexpr const char *BaseName = "SimplifyCFG";
  using Pass = optim::SimplifyCFG;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct LVNConf : public FunctionPassConf<LVNConf> {
  static constexpr const char *BaseName = "LVN";
  using Pass = optim::LVN;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct CmpKnownValPropConf : public FunctionPassConf<CmpKnownValPropConf> {
  static constexpr const char *BaseName = "CmpKnownValProp";
  using Pass = optim::CmpKnownValProp;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct TailRecElimConf : public FunctionPassConf<TailRecElimConf> {
  static constexpr const char *BaseName = "TailRecElim";
  using Pass = optim::TailRecElim;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct LICMConf : public FunctionPassConf<LICMConf> {
  static constexpr const char *BaseName = "LICM";
  using Pass = optim::LICM;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct LoopRotateConf : public FunctionPassConf<LoopRotateConf> {
  static constexpr const char *BaseName = "LoopRotate";
  using Pass = optim::LoopRotate;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct LoopSimplifyConf : public FunctionPassConf<LoopSimplifyConf> {
  static constexpr const char *BaseName = "LoopSimplify";
  using Pass = optim::LoopSimplify;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct SCCPConf : public FunctionPassConf<SCCPConf> {
  static constexpr const char *BaseName = "SCCP";
  using Pass = optim::SCCP;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct StackKnownBitsConf : public FunctionPassConf<StackKnownBitsConf> {
  static constexpr const char *BaseName = "StackKnownBits";
  using Pass = optim::StackKnownBits;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct ConstLoopEvalConf : public FunctionPassConf<ConstLoopEvalConf> {
  static constexpr const char *BaseName = "ConstLoopEval";
  using Pass = optim::ConstLoopEval;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct SLPVectorizerConf : public FunctionPassConf<SLPVectorizerConf>,
                           optim::SLPVectorizer::Config {
  static constexpr const char *BaseName = "SLPVectorizer";
  using Pass = optim::SLPVectorizer;
  bool pass_parse(toml::table &tbl) {
    reductions = tbl["reductions"].value_or(reductions);
    return true;
  }
  void construct_function_pass(Pass &p) {
    p.config = *static_cast<Pass::Config *>(this);
  };
};
struct MergeAllocaConf : public FunctionPassConf<MergeAllocaConf> {
  static constexpr const char *BaseName = "MergeAlloca";
  using Pass = optim::MergeAllocaPass;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct LegalizeVecsConf : public FunctionPassConf<LegalizeVecsConf> {
  static constexpr const char *BaseName = "LegalizeVecs";
  using Pass = optim::LegalizeVecs;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct LoopUnswitchConf : public FunctionPassConf<LoopUnswitchConf> {
  static constexpr const char *BaseName = "LoopUnswitch";
  using Pass = optim::LoopUnswitch;
  bool pass_parse(toml::table &) { return true; }
  void construct_function_pass(Pass &) {};
};
struct LoopUnrollConf : public FunctionPassConf<LoopUnrollConf>,
                        optim::LoopUnroll::Config {
  static constexpr const char *BaseName = "LoopUnroll";
  using Pass = optim::LoopUnroll;

  bool pass_parse(toml::table &tbl) {
    max_unroll = tbl["maxUnroll"].value_or(max_unroll);
    max_instr = tbl["maxInstr"].value_or(max_instr);
    return true;
  }
  void construct_function_pass(Pass &p) {
    p.config = *static_cast<Pass::Config *>(this);
  };
};

// #########################################################################################

struct InlineConf : public ModulePassConf<InlineConf>, optim::Inline<>::Config {
  static constexpr const char *BaseName = "Inline";
  using Pass = optim::Inline<>;

  bool pass_parse(toml::table &tbl) {
    recursive = tbl["recursive"].value_or(recursive);
    return true;
  }

  void construct_module_pass(Pass &p) {
    p.config = *static_cast<Pass::Config *>(this);
  };
};

struct FuncPropAnnotatorConf : public ModulePassConf<FuncPropAnnotatorConf> {
  static constexpr const char *BaseName = "FuncPropAnnotator";
  using Pass = optim::FuncPropAnnotator;
  bool pass_parse(toml::table &) { return true; }
  void construct_module_pass(Pass &) {};
};
struct GlobalPromotionConf : public ModulePassConf<GlobalPromotionConf> {
  static constexpr const char *BaseName = "GlobalPromotion";
  using Pass = optim::GlobalPromotion;
  bool pass_parse(toml::table &) { return true; }
  void construct_module_pass(Pass &) {};
};
struct ArgPromotionConf : public ModulePassConf<ArgPromotionConf> {
  static constexpr const char *BaseName = "ArgPromotion";
  using Pass = optim::ArgPromotion;
  bool pass_parse(toml::table &) { return true; }
  void construct_module_pass(Pass &) {};
};
struct GDCEConf : public ModulePassConf<GDCEConf> {
  static constexpr const char *BaseName = "GDCE";
  using Pass = optim::GDCE;
  bool pass_parse(toml::table &) { return true; }
  void construct_module_pass(Pass &) {};
};
struct IPCPConf : public ModulePassConf<IPCPConf> {
  static constexpr const char *BaseName = "IPCP";
  using Pass = optim::IPCP;
  bool pass_parse(toml::table &) { return true; }
  void construct_module_pass(Pass &) {};
};
struct FunctionDedupSameConf : public ModulePassConf<FunctionDedupSameConf> {
  static constexpr const char *BaseName = "FunctionDedupSame";
  using Pass = optim::FunctionDeDup<true>;
  bool pass_parse(toml::table &) { return true; }
  void construct_module_pass(Pass &) {};
};
struct FunctionDedupDiffConf : public ModulePassConf<FunctionDedupDiffConf> {
  static constexpr const char *BaseName = "FunctionDedupDiff";
  using Pass = optim::FunctionDeDup<false>;
  bool pass_parse(toml::table &) { return true; }
  void construct_module_pass(Pass &) {};
};

} // namespace foptim::conf
