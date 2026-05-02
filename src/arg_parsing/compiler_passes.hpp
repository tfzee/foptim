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
#include "optim/func_passes/sccp.hpp"
#include "optim/func_passes/simplify_cfg.hpp"
#include "optim/func_passes/slp_vectorizer.hpp"
#include "optim/func_passes/sora.hpp"
#include "optim/func_passes/stack_known_bits.hpp"
#include "optim/func_passes/tail_rec_elim.hpp"
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
    std::declval<T>().construct_module_pass(std::declval<typename T::Pass&>())
  } -> std::same_as<void>;
};
template <typename T>
concept has_construct_function_pass_func = requires {
  {
    std::declval<T>().construct_function_pass(std::declval<typename T::Pass&>())
  } -> std::same_as<void>;
};

template <class T>
struct ModulePassConf : public PassConfig {
  virtual std::string_view get_name() const override final { return T::Name; }
  virtual PassType pass_type() const override final { return PassType::Module; }
  virtual bool _pass_parse(void* arg) override final {
    return ((T*)this)->pass_parse(*(toml::table*)arg);
  }
  virtual optim::ModulePass* _construct_module_pass() override {
    auto* alloc = utils::IRAlloc<typename T::Pass>{}.allocate(1);
    new (alloc) T::Pass{};
    static_assert(
        std::is_convertible<typename T::Pass*, optim::ModulePass*>::value,
        "The pass must inherit from module pass from public");
    static_assert(has_construct_module_pass_func<T>,
                  "When inheriting from ModulePassConfig you gotta implement "
                  "the construct_module_pass function");
    ((T*)this)->construct_module_pass(*alloc);
    return (optim::ModulePass*)alloc;
  };
  virtual optim::FunctionPass* _construct_function_pass() override {
    TODO("INVALID TYPE OF PASS");
  };
};

template <class T>
struct FunctionPassConf : public PassConfig {
  virtual std::string_view get_name() const override final { return T::Name; }
  virtual PassType pass_type() const override final {
    return PassType::Function;
  }
  virtual bool _pass_parse(void* arg) override final {
    return ((T*)this)->pass_parse(*(toml::table*)arg);
  }
  virtual optim::ModulePass* _construct_module_pass() override {
    TODO("INVALID TYPE OF PASS");
  };
  virtual optim::FunctionPass* _construct_function_pass() override {
    auto* alloc = utils::IRAlloc<typename T::Pass>{}.allocate(1);
    new (alloc) T::Pass{};
    static_assert(
        std::is_convertible<typename T::Pass*, optim::FunctionPass*>::value,
        "The pass must inherit from module pass from public");
    static_assert(has_construct_function_pass_func<T>,
                  "When inheriting from FunctionPassConfig you gotta implement "
                  "the construct_function_pass function");
    ((T*)this)->construct_function_pass(*alloc);
    return (optim::FunctionPass*)alloc;
  };
};

struct DCEConf : public FunctionPassConf<DCEConf> {
  static constexpr const char* Name = "DCE";
  using Pass = optim::DCE;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct LegalizeStructsConf : public FunctionPassConf<LegalizeStructsConf> {
  static constexpr const char* Name = "LegalizeStructs";
  using Pass = optim::LegalizeStructs;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct LLVMIntrinsicLoweringConf
    : public FunctionPassConf<LLVMIntrinsicLoweringConf> {
  static constexpr const char* Name = "LLVMIntrinsicLowering";
  using Pass = optim::LLVMInstrinsicLowering;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct SORAConf : public FunctionPassConf<SORAConf> {
  static constexpr const char* Name = "SORA";
  using Pass = optim::SORA;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct Mem2RegConf : public FunctionPassConf<Mem2RegConf> {
  static constexpr const char* Name = "Mem2Reg";
  using Pass = optim::Mem2Reg;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct DoubleLoadElimConf : public FunctionPassConf<DoubleLoadElimConf> {
  static constexpr const char* Name = "DoubleLoadElim";
  using Pass = optim::DoubleLoadElim;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct IntrinSimplifyConf : public FunctionPassConf<IntrinSimplifyConf> {
  static constexpr const char* Name = "IntrinSimplify";
  using Pass = optim::IntrinSimplify;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct InstSimplifyConf : public FunctionPassConf<InstSimplifyConf> {
  static constexpr const char* Name = "InstSimplify";
  using Pass = optim::InstSimplify;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct SimplifyCFGConf : public FunctionPassConf<SimplifyCFGConf> {
  static constexpr const char* Name = "SimplifyCFG";
  using Pass = optim::SimplifyCFG;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct LVNConf : public FunctionPassConf<LVNConf> {
  static constexpr const char* Name = "LVN";
  using Pass = optim::LVN;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct CmpKnownValPropConf : public FunctionPassConf<CmpKnownValPropConf> {
  static constexpr const char* Name = "CmpKnownValProp";
  using Pass = optim::CmpKnownValProp;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct TailRecElimConf : public FunctionPassConf<TailRecElimConf> {
  static constexpr const char* Name = "TailRecElim";
  using Pass = optim::TailRecElim;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct LICMConf : public FunctionPassConf<LICMConf> {
  static constexpr const char* Name = "LICM";
  using Pass = optim::LICM;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct LoopRotateConf : public FunctionPassConf<LoopRotateConf> {
  static constexpr const char* Name = "LoopRotate";
  using Pass = optim::LoopRotate;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct LoopSimplifyConf : public FunctionPassConf<LoopSimplifyConf> {
  static constexpr const char* Name = "LoopSimplify";
  using Pass = optim::LoopSimplify;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct SCCPConf : public FunctionPassConf<SCCPConf> {
  static constexpr const char* Name = "SCCP";
  using Pass = optim::SCCP;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct StackKnownBitsConf : public FunctionPassConf<StackKnownBitsConf> {
  static constexpr const char* Name = "StackKnownBits";
  using Pass = optim::StackKnownBits;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct ConstLoopEvalConf : public FunctionPassConf<ConstLoopEvalConf> {
  static constexpr const char* Name = "ConstLoopEval";
  using Pass = optim::ConstLoopEval;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct SLPVectorizerConf : public FunctionPassConf<SLPVectorizerConf>,
                           optim::SLPVectorizer::Config {
  static constexpr const char* Name = "SLPVectorizer";
  using Pass = optim::SLPVectorizer;
  bool pass_parse(toml::table& tbl) {
    reductions = tbl["reductions"].value_or(reductions);
    return true;
  }
  void construct_function_pass(Pass& p) {
    p.config = static_cast<Pass::Config>(*this);
  };
};
struct MergeAllocaConf : public FunctionPassConf<MergeAllocaConf> {
  static constexpr const char* Name = "MergeAlloca";
  using Pass = optim::MergeAllocaPass;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct LegalizeVecsConf : public FunctionPassConf<LegalizeVecsConf> {
  static constexpr const char* Name = "LegalizeVecs";
  using Pass = optim::LegalizeVecs;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct LoopUnswitchConf : public FunctionPassConf<LoopUnswitchConf> {
  static constexpr const char* Name = "LoopUnswitch";
  using Pass = optim::LoopUnswitch;
  bool pass_parse(toml::table&) { return true; }
  void construct_function_pass(Pass&) {};
};
struct LoopUnrollConf : public FunctionPassConf<LoopUnrollConf>,
                        optim::LoopUnroll::Config {
  static constexpr const char* Name = "LoopUnroll";
  using Pass = optim::LoopUnroll;

  bool pass_parse(toml::table& tbl) {
    max_unroll = tbl["maxUnroll"].value_or(max_unroll);
    max_instr = tbl["maxInstr"].value_or(max_instr);
    return true;
  }
  void construct_function_pass(Pass& p) {
    p.config = *static_cast<Pass::Config*>(this);
  };
};

// #########################################################################################

struct InlineConf : public ModulePassConf<InlineConf>, optim::Inline<>::Config {
  static constexpr const char* Name = "Inline";
  using Pass = optim::Inline<>;

  bool pass_parse(toml::table& tbl) {
    recursive = tbl["recursive"].value_or(recursive);
    return true;
  }

  void construct_module_pass(Pass& p) {
    p.config = *static_cast<Pass::Config*>(this);
  };
};

struct FuncPropAnnotatorConf : public ModulePassConf<FuncPropAnnotatorConf> {
  static constexpr const char* Name = "FuncPropAnnotator";
  using Pass = optim::FuncPropAnnotator;
  bool pass_parse(toml::table&) { return true; }
  void construct_module_pass(Pass&) {};
};
struct GlobalPromotionConf : public ModulePassConf<GlobalPromotionConf> {
  static constexpr const char* Name = "GlobalPromotion";
  using Pass = optim::GlobalPromotion;
  bool pass_parse(toml::table&) { return true; }
  void construct_module_pass(Pass&) {};
};
struct ArgPromotionConf : public ModulePassConf<ArgPromotionConf> {
  static constexpr const char* Name = "ArgPromotion";
  using Pass = optim::ArgPromotion;
  bool pass_parse(toml::table&) { return true; }
  void construct_module_pass(Pass&) {};
};
struct GDCEConf : public ModulePassConf<GDCEConf> {
  static constexpr const char* Name = "GDCE";
  using Pass = optim::GDCE;
  bool pass_parse(toml::table&) { return true; }
  void construct_module_pass(Pass&) {};
};
struct IPCPConf : public ModulePassConf<IPCPConf> {
  static constexpr const char* Name = "IPCP";
  using Pass = optim::IPCP;
  bool pass_parse(toml::table&) { return true; }
  void construct_module_pass(Pass&) {};
};
struct FunctionDedupSameConf : public ModulePassConf<FunctionDedupSameConf> {
  static constexpr const char* Name = "FunctionDedupSame";
  using Pass = optim::FunctionDeDup<true>;
  bool pass_parse(toml::table&) { return true; }
  void construct_module_pass(Pass&) {};
};
struct FunctionDedupDiffConf : public ModulePassConf<FunctionDedupDiffConf> {
  static constexpr const char* Name = "FunctionDedupDiff";
  using Pass = optim::FunctionDeDup<false>;
  bool pass_parse(toml::table&) { return true; }
  void construct_module_pass(Pass&) {};
};

}  // namespace foptim::conf
