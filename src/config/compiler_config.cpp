#include "compiler_config.hpp"

#include <fmt/base.h>

#include <iostream>
#include <string_view>
#include <third_party/toml.hpp>

#include "compiler_mir_passes.hpp"
#include "compiler_passes.hpp"
#include "utils/string.hpp"
#include "utils/todo.hpp"
namespace foptim::conf {

namespace {

bool target_parse(Target& target, toml::node_view<toml::node> cnf) {
  target.name = cnf["name"].value_or(target.name);
  auto features = cnf["features"];
  target.features.avx512f =
      features["avx512f"].value_or(target.features.avx512f);
  target.features.avx512bw =
      features["avx512bw"].value_or(target.features.avx512bw);
  target.features.avx512cd =
      features["avx512cd"].value_or(target.features.avx512cd);
  target.features.avx512dq =
      features["avx512dq"].value_or(target.features.avx512dq);
  target.features.avx512vl =
      features["avx512vl"].value_or(target.features.avx512vl);
  target.features.bmi2 = features["bmi2"].value_or(target.features.bmi2);
  return true;
}

bool optimize_float_parse(Optimize::FloatOptions& flt,
                          toml::node_view<toml::node> cnf) {
  flt.no_nans = cnf["no_nans"].value_or(flt.no_nans);
  flt.no_infinites = cnf["no_infinites"].value_or(flt.no_infinites);
  flt.associative_math = cnf["associative_math"].value_or(flt.associative_math);
  flt.reciprocal_math = cnf["reciprocal_math"].value_or(flt.reciprocal_math);
  flt.no_signed_zeros = cnf["no_signed_zeros"].value_or(flt.no_signed_zeros);
  flt.no_math_errno = cnf["no_math_errno"].value_or(flt.no_math_errno);
  flt.no_trapping_math = cnf["no_trapping_math"].value_or(flt.no_trapping_math);
  flt.no_rounding_mode = cnf["no_rounding_mode"].value_or(flt.no_rounding_mode);
  flt.fast_contract = cnf["fast_contract"].value_or(flt.fast_contract);
  flt.approx_func = cnf["approx_func"].value_or(flt.approx_func);
  return true;
}

bool optimize_parse(Optimize& optim, CompConf& conf,
                    toml::node_view<toml::node> cnf) {
  auto v_fir = cnf["fir_pipeline"].value<std::string_view>();
  if (v_fir.has_value()) {
    optim.fir_pipeline = conf.find_pipeline<IRType::FIR>(v_fir.value());
  }
  auto v_mir = cnf["mir_pipeline"].value<std::string_view>();
  if (v_mir.has_value()) {
    optim.mir_pipeline = conf.find_pipeline<IRType::MIR>(v_mir.value());
  }
  optim.all_linkage_internal =
      cnf["all_linkage_internal"].value_or(optim.all_linkage_internal);
  optim.assume_cstdlib_beheaviour = cnf["assume_cstdlib_beheaviour"].value_or(
      optim.assume_cstdlib_beheaviour);
  if (cnf["float"].is_table()) {
    optimize_float_parse(optim.fltOpt, cnf["float"]);
  }
  return true;
}

struct PassesArray {
  const char* name;
  PassConfig config;
};

template <IRType Ty>
std::optional<PassConfig*> setup_pass(std::string_view name, toml::table* cnf) {
  PassConfig* pass = nullptr;
  if constexpr (Ty == IRType::FIR) {
    // TODO: cleanup of these
    // TODO: really shouldnt allocate here
    if (name == "Inline") {
      pass = new InlineConf{};
    } else if (name == "DCE") {
      pass = new DCEConf{};
    } else if (name == "LegalizeStructs") {
      pass = new LegalizeStructsConf{};
    } else if (name == "LLVMIntrinsicLowering") {
      pass = new LLVMIntrinsicLoweringConf{};
    } else if (name == "SORA") {
      pass = new SORAConf{};
    } else if (name == "Mem2Reg") {
      pass = new Mem2RegConf{};
    } else if (name == "DoubleLoadElim") {
      pass = new DoubleLoadElimConf{};
    } else if (name == "IntrinSimplify") {
      pass = new IntrinSimplifyConf{};
    } else if (name == "InstSimplify") {
      pass = new InstSimplifyConf{};
    } else if (name == "SimplifyCFG") {
      pass = new SimplifyCFGConf{};
    } else if (name == "LVN") {
      pass = new LVNConf{};
    } else if (name == "CmpKnownValProp") {
      pass = new CmpKnownValPropConf{};
    } else if (name == "TailRecElim") {
      pass = new TailRecElimConf{};
    } else if (name == "LICM") {
      pass = new LICMConf{};
    } else if (name == "LoopRotate") {
      pass = new LoopRotateConf{};
    } else if (name == "LoopSimplify") {
      pass = new LoopSimplifyConf{};
    } else if (name == "LVN") {
      pass = new LVNConf{};
    } else if (name == "LegalizeVecs") {
      pass = new LegalizeVecsConf{};
    } else if (name == "MergeAlloca") {
      pass = new MergeAllocaConf{};
    } else if (name == "SCCP") {
      pass = new SCCPConf{};
    } else if (name == "StackKnownBits") {
      pass = new StackKnownBitsConf{};
    } else if (name == "SLPVectorizer") {
      pass = new SLPVectorizerConf{};
    } else if (name == "SCCP") {
      pass = new SCCPConf{};
    } else if (name == "ConstLoopEval") {
      pass = new ConstLoopEvalConf{};
    } else if (name == "FuncPropAnnotator") {
      pass = new FuncPropAnnotatorConf{};
    } else if (name == "GlobalPromotion") {
      pass = new GlobalPromotionConf{};
    } else if (name == "ArgPromotion") {
      pass = new ArgPromotionConf{};
    } else if (name == "GDCE") {
      pass = new GDCEConf{};
    } else if (name == "IPCP") {
      pass = new IPCPConf{};
    } else if (name == "FunctionDedupSame") {
      pass = new FunctionDedupSameConf{};
    } else if (name == "FunctionDedupDiff") {
      pass = new FunctionDedupDiffConf{};
    } else if (name == "LoopUnswitch") {
      pass = new LoopUnswitchConf{};
    } else if (name == "LoopUnroll") {
      pass = new LoopUnrollConf{};
    } else if (name == "PrintFunc") {
      pass = new PrintFuncConf{};
    } else if (name == "VerifyFunc") {
      pass = new VerifyFuncConf{};
    } else {
      fmt::println("Dont know any fir pass with the name '{}'", name);
      TODO("Dont know this pass name");
    }
  } else {
    if (name == "DCE") {
      pass = new fmir::DCEConf{};
    } else if (name == "LegalizeBBForm") {
      pass = new fmir::LegalizeBBConf{};
    } else if (name == "BBReordering") {
      pass = new fmir::BBReorderingConf{};
    } else {
      fmt::println("Dont know any mir pass with the name '{}'", name);
      TODO("Dont know this pass name");
    }
  }
  ASSERT(pass != nullptr);
  pass->_pass_parse(cnf);
  return pass;
}

template <IRType Ty>
bool passes_parse(CompConf& conf, toml::table& tbl) {
  auto& target_arr = Ty == IRType::FIR ? conf.fir_passes : conf.mir_passes;
  for (auto&& [name, data] : tbl) {
    auto str_name = name.str();
    auto end = target_arr.end();
    for (auto i = target_arr.begin(); i != end; ++i) {
      auto pass_ptr = *(*i).get_raw_ptr();
      if (pass_ptr->get_name() == str_name) {
        if (!pass_ptr->_pass_parse(&data)) {
          return false;
        }
      }
    }
    auto p = setup_pass<Ty>(name.str(), data.as_table());
    if (!p) {
      return false;
    }
    target_arr.push_back(p.value());
  }
  return true;
}

template <IRType Ty>
bool pipeline_parse(PipelineRef pipeline, CompConf& conf,
                    toml::node_view<toml::node> cnf) {
  if (cnf["passes"].is_array()) {
    pipeline->passes.clear();
    for (auto&& pass_name_val : *cnf["passes"].as_array()) {
      auto maybe_pass_name = pass_name_val.value<std::string_view>();
      ASSERT(maybe_pass_name.has_value());
      auto pass_name = maybe_pass_name.value();
      if (pass_name.starts_with("pipeline:")) {
        auto real_name = pass_name.substr(9);
        auto found_pass = conf.find_pipeline<Ty>(real_name);
        pipeline->passes.push_back({found_pass});
      } else {
        auto found_pass = conf.find_pass<Ty>(pass_name);
        pipeline->passes.push_back({found_pass});
      }
    }
  }
  return true;
}

template <IRType Ty>
bool pipelines_parse(CompConf& conf, toml::table& tbl) {
  TVec<std::pair<std::string_view, toml::node_view<toml::node>>> overwrites;
  auto& target_arr =
      Ty == IRType::FIR ? conf.fir_pipelines : conf.mir_pipelines;
  // first push them all back and make sure there all in
  for (auto&& [name, data] : tbl) {
    auto str_name = name.str();
    auto end = target_arr.end();
    bool found = false;
    for (auto i = target_arr.begin(); i != end; ++i) {
      if (i->name == str_name) {
        found = true;
        break;
      }
    }
    if (found) {
      continue;
    }
    Pipeline p;
    p.name = str_name;
    target_arr.push_back(p);
  }

  for (auto&& [name, data] : tbl) {
    auto str_name = name.str();
    auto end = target_arr.end();
    for (auto i = target_arr.begin(); i != end; ++i) {
      if (i->name == str_name) {
        if (!pipeline_parse<Ty>({*i}, conf, toml::node_view(data))) {
          return false;
        }
        break;
      }
    }
  }
  return true;
}

bool debug_parse(Debug& conf, toml::table& tbl) {
  conf.bisect = tbl["bisect"].value_or(conf.bisect);
  conf.print_color = tbl["print_color"].value_or(conf.print_color);
  conf.print_optimization_failure_reasons =
      tbl["print_optimization_failure_reasons"].value_or(
          conf.print_optimization_failure_reasons);
  conf.print_between_passes =
      tbl["print_between_passes"].value_or(conf.print_between_passes);
  conf.verify_between_passes =
      tbl["verify_between_passes"].value_or(conf.verify_between_passes);

  return true;
}

bool include_parse(CompConf& conf, toml::array& arr) {
  for (auto&& include : arr) {
    auto maybe_name = include.value<std::string_view>();
    ASSERT(maybe_name.has_value());
    auto name = maybe_name.value();
    conf.parse(name.begin());
  }
  return true;
}

bool config_parse(CompConf& conf, toml::table& tbl) {
  uint64_t version = tbl["config_version"].value_or(1);
  ASSERT(version == 1);
  target_parse(conf.target, tbl["target"]);
  if (tbl["include"].is_array()) {
    if (!include_parse(conf, *tbl["include"].as_array())) {
      return false;
    }
  }
  if (tbl["pass"]["fir"].is_table()) {
    if (!passes_parse<IRType::FIR>(conf, *tbl["pass"]["fir"].as_table())) {
      return false;
    }
  }
  if (tbl["pass"]["mir"].is_table()) {
    if (!passes_parse<IRType::MIR>(conf, *tbl["pass"]["mir"].as_table())) {
      return false;
    }
  }
  if (tbl["pipeline"]["fir"].is_table()) {
    if (!pipelines_parse<IRType::FIR>(conf,
                                      *tbl["pipeline"]["fir"].as_table())) {
      return false;
    }
  }
  if (tbl["pipeline"]["mir"].is_table()) {
    if (!pipelines_parse<IRType::MIR>(conf,
                                      *tbl["pipeline"]["mir"].as_table())) {
      return false;
    }
  }
  if (tbl["debug"].is_table()) {
    if (!debug_parse(conf.debug, *tbl["debug"].as_table())) {
      return false;
    }
  }
  if (tbl["optimize"].is_table()) {
    if (!optimize_parse(conf.optim, conf, tbl["optimize"])) {
      return false;
    }
  }
  return true;
}

static const char default_toml[] = {
#embed "default.toml"
    , 0};
static const char fast_math_toml[] = {
#embed "fast_math.toml"
    , 0};

static constexpr const char* builtin_configs[] = {
    &default_toml[0],
    &fast_math_toml[0],
};

enum class BuiltinConfig {
  Default = 0,
  FastMath = 1,
};

void setup_builtin(CompConf& conf, BuiltinConfig config) {
  toml::table tbl;
  try {
    const char* config_ptr = builtin_configs[(u32)config];
    auto view = std::string_view{config_ptr};
    tbl = toml::parse(view);
  } catch (const toml::parse_error& err) {
    fmt::println("{};  of default {}", err, (u32)config);
    TODO("FAILED PARSE Default {}");
  }
  config_parse(conf, tbl);
  // conf.parse("../src/default.toml");
}

}  // namespace

bool CompConf::parse(std::string_view filename) {
  using namespace std::literals;

  toml::table tbl;
  if (filename == "Default") {
    setup_builtin(*this, BuiltinConfig::Default);
    return true;
  }
  if (filename == "FastMath") {
    setup_builtin(*this, BuiltinConfig::FastMath);
    return true;
  }
  try {
    tbl = toml::parse_file(filename);
  } catch (const toml::parse_error& err) {
    std::cerr << "Error parsing config file '" << filename << "':\n"
              << err << "\n";
    TODO("FAILED PARSE");
  }
  config_parse(*this, tbl);

  return true;
}
}  // namespace foptim::conf
