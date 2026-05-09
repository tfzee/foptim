#pragma once
#include <fmt/base.h>

#include <string_view>

#include "mir/optim/function_pass.hpp"
#include "utils/stable_vec.hpp"
#include "utils/string.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::conf {

enum class IRType : u8 {
  FIR,
  MIR,
};

struct Target {
  FString name;
  struct Features {
    bool avx512f;
    bool avx512bw;
    bool avx512cd;
    bool avx512dq;
    bool avx512vl;

    bool bmi2;
  } features;
};

struct PassConfig;
struct PassRef : utils::SRef<PassConfig *> {};
struct Pipeline;
struct PipelineRef : utils::SRef<Pipeline> {};

struct PassConfig {
  enum PassType {
    FIR_Function,
    FIR_Module,
    MIR_Func,
  };
  virtual std::string_view get_name() const { TODO("IMPL"); };
  virtual PassType pass_type() const { TODO("IMPL"); };
  virtual bool _pass_parse(void *) { TODO("IMPL"); };
  virtual optim::ModulePass *_construct_module_pass() {
    TODO("INVALID TYPE OF PASS");
    ;
  };
  virtual optim::FunctionPass *_construct_function_pass() {
    TODO("INVALID TYPE OF PASS");
    ;
  };
  virtual fmir::FunctionPass *_construct_mir_func_pass() {
    TODO("INVALID TYPE OF PASS");
    ;
  };
};

struct PipelineElem {
  enum Type {
    Pipeline,
    Pass,
  } type;
  union {
    PipelineRef pipeline;
    PassRef pass;
  };

  PipelineElem(PassRef r) : type(Type::Pass), pass(r) {}
  PipelineElem(PipelineRef r) : type(Type::Pipeline), pipeline(r) {}
};

struct Pipeline {
  FString name;
  FVec<PipelineElem> passes;
};

struct Optimize {
  PipelineRef fir_pipeline;
  PipelineRef mir_pipeline;
  bool all_linkage_internal;
  bool assume_cstdlib_beheaviour;

  struct FloatOptions {
    bool no_nans;
    bool no_infinites;
    bool associative_math;
    bool reciprocal_math;
    bool no_signed_zeros;
    bool no_math_errno;
    bool no_trapping_math;
    bool no_rounding_mode;
    bool fast_contract;
    bool approx_func;
  };
  FloatOptions fltOpt;
};

struct Debug {
  u32 bisect;
  bool print_between_passes;
  bool print_optimization_failure_reasons;
  bool verify_between_passes;
  bool print_color;
};

struct Remarks {};

struct BasePassesData {
  const char *name;
  PassConfig config;
};

struct CompConf {
  Target target;
  Debug debug;
  Optimize optim;
  Remarks remarks;

  utils::StableVec<Pipeline> mir_pipelines;
  utils::StableVec<Pipeline> fir_pipelines;
  utils::StableVec<PassConfig *> fir_passes;
  utils::StableVec<PassConfig *> mir_passes;

  CompConf() {}

  template <IRType Ty> PipelineRef find_pipeline(std::string_view name) {
    auto &pipelines = Ty == IRType::FIR ? fir_pipelines : mir_pipelines;
    for (auto pipe : pipelines) {
      if (pipe->name == name) {
        return {pipe};
      }
    }
    constexpr auto ty_name = Ty == IRType::FIR ? "FIR" : "MIR";
    fmt::println("Searched for {} pipeline with name '{}'", ty_name, name);
    fmt::println("Available {} Pipelines:", ty_name);
    for (auto pipe : pipelines) {
      fmt::println("- {}", pipe->name);
    }
    TODO("Failed to find pipeline with that name");
  }
  template <IRType Ty> PassRef find_pass(std::string_view name) {
    auto &passes = Ty == IRType::FIR ? fir_passes : mir_passes;
    for (auto pass : passes) {
      if (name == (*pass.get_raw_ptr())->get_name()) {
        return {pass};
      }
    }
    constexpr auto ty_name = Ty == IRType::FIR ? "FIR" : "MIR";
    fmt::println("Searched for {} pass with name '{}'", ty_name, name);
    fmt::println("Available {} Passes:", ty_name);
    for (auto pass : passes) {
      fmt::println("- {}", (*pass.get_raw_ptr())->get_name());
    }
    TODO("Failed to find pass with that name");
  }

  bool parse(std::string_view filename);
};

} // namespace foptim::conf
