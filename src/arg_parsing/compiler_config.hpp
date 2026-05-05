#pragma once
#include <string_view>

#include "utils/stable_vec.hpp"
#include "utils/string.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"

namespace foptim::conf {

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
struct PassRef : utils::SRef<PassConfig*> {};
struct Pipeline;
struct PipelineRef : utils::SRef<Pipeline> {};

struct PassConfig {
  enum PassType {
    Function,
    Module,
  };
  virtual std::string_view get_name() const { TODO("IMPL"); };
  virtual PassType pass_type() const { TODO("IMPL"); };
  virtual bool _pass_parse(void*) { TODO("IMPL"); };
  virtual optim::ModulePass* _construct_module_pass() { TODO("IMPL"); };
  virtual optim::FunctionPass* _construct_function_pass() { TODO("IMPL"); };
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
  FVec<PipelineElem> fir_passes;
};

struct Optimize {
  PipelineRef pipeline;
  bool all_linkage_internal;
  bool assume_cstdlib_beheaviour;

  struct FloatOptions{
      bool no_rounding_math;
      bool no_nans;
      bool no_infinites;
      bool associative_math;
      bool reciprocal_math;
      bool no_signed_zeros;
      bool no_math_errno;
      bool no_trapping_math;
      bool no_rounding_mode;
      bool contract;
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
  const char* name;
  PassConfig config;
};

struct CompConf {
  Target target;
  Debug debug;
  Optimize optim;
  Remarks remarks;

  utils::StableVec<Pipeline> pipelines;
  utils::StableVec<PassConfig*> passes;

  CompConf() {}

  PipelineRef find_pipeline(std::string_view);
  PassRef find_pass(std::string_view);

  bool parse(std::string_view filename);
};

}  // namespace foptim::conf
