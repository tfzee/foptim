#pragma once
#include <fmt/base.h>

#include <optional>
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

struct Input {
  // empty = piped in i guess
  IRString in_file;
};

struct Output {
  enum class OutputType : u8 { Object, PrintIR, PrintMIR, Assembly };
  OutputType type = OutputType::Object;
  // empty = stdout
  IRString out_file;
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

  FString override_name;
  [[nodiscard]] virtual PassConfig *clone() const = 0;
  [[nodiscard]] virtual std::string_view get_name() const = 0;
  [[nodiscard]] virtual PassType pass_type() const = 0;
  virtual bool _pass_parse(void *) = 0;
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
  i32 bisect;
  bool print_between_passes;
  bool print_optimization_failure_reasons;
  bool verify_between_passes;
  bool time_passes;
  bool print_color;
  u8 verbosity;
  // keep functions in input order instead of sorting them for MIR lowering
  bool no_reorder_funcs = false;
};

struct Remarks {};

struct CompConf {
  Target target;
  Input input;
  Output output;
  Debug debug;
  Optimize optim;
  Remarks remarks;

  u8 number_worker_threads = 0;

  utils::StableVec<Pipeline> mir_pipelines;
  utils::StableVec<Pipeline> fir_pipelines;
  utils::StableVec<PassConfig *> fir_passes;
  utils::StableVec<PassConfig *> mir_passes;

  CompConf() = default;

  template <IRType Ty> std::optional<PipelineRef> lookup_pipeline(std::string_view name) {
    auto &pipelines = Ty == IRType::FIR ? fir_pipelines : mir_pipelines;
    for (auto pipe : pipelines) {
      if (pipe->name == name) {
        return PipelineRef{pipe};
      }
    }
    return std::nullopt;
  }
  template <IRType Ty> std::optional<PassRef> lookup_pass(std::string_view name) {
    auto &passes = Ty == IRType::FIR ? fir_passes : mir_passes;
    for (auto pass : passes) {
      if (name == (*pass.get_raw_ptr())->get_name()) {
        return PassRef{pass};
      }
    }
    return std::nullopt;
  }
  template <IRType Ty> void print_available_pipelines() {
    auto &pipelines = Ty == IRType::FIR ? fir_pipelines : mir_pipelines;
    constexpr auto ty_name = Ty == IRType::FIR ? "FIR" : "MIR";
    fmt::println(stderr, "Available {} Pipelines:", ty_name);
    for (auto pipe : pipelines) {
      fmt::println(stderr, "- {}", pipe->name);
    }
  }
  template <IRType Ty> void print_available_passes() {
    auto &passes = Ty == IRType::FIR ? fir_passes : mir_passes;
    constexpr auto ty_name = Ty == IRType::FIR ? "FIR" : "MIR";
    fmt::println(stderr, "Available {} Passes:", ty_name);
    for (auto pass : passes) {
      fmt::println(stderr, "- {}", (*pass.get_raw_ptr())->get_name());
    }
  }

  template <IRType Ty> PipelineRef find_pipeline(std::string_view name) {
    if (auto r = lookup_pipeline<Ty>(name)) {
      return *r;
    }
    constexpr auto ty_name = Ty == IRType::FIR ? "FIR" : "MIR";
    fmt::println("Searched for {} pipeline with name '{}'", ty_name, name);
    print_available_pipelines<Ty>();
    TODO("Failed to find pipeline with that name");
  }
  template <IRType Ty> PassRef find_pass(std::string_view name) {
    if (auto r = lookup_pass<Ty>(name)) {
      return *r;
    }
    constexpr auto ty_name = Ty == IRType::FIR ? "FIR" : "MIR";
    fmt::println("Searched for {} pass with name '{}'", ty_name, name);
    print_available_passes<Ty>();
    TODO("Failed to find pass with that name");
  }

  // Overrides the active FIR pipeline with a comma separated list of
  // registered passes / "pipeline:<name>" entries. With `append` the list is
  // added behind the currently active pipeline. Must run after the config was
  // parsed. Prints the problem to stderr and returns false on unknown names.
  bool set_fir_pass_list(std::string_view list, bool append);

  bool parse(std::string_view filename);
};

} // namespace foptim::conf
