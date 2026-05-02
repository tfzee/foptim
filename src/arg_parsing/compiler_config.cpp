#include "compiler_config.hpp"

#include <fmt/base.h>

#include <iostream>
#include <string_view>
#include <third_party/toml.hpp>

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

bool optimize_parse(Optimize& optim, CompConf& conf,
                    toml::node_view<toml::node> cnf) {
  auto v = cnf["pipeline"].value<std::string_view>();
  if (v.has_value()) {
    optim.pipeline = conf.find_pipeline(v.value());
  }
  return true;
}

struct PassesArray {
  const char* name;
  PassConfig config;
};

std::optional<PassConfig*> setup_pass(std::string_view name, toml::table* cnf) {
  PassConfig* pass = nullptr;
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
  } else {
    fmt::println("Dont know any pass with the name '{}'", name);
    TODO("Dont know this pass name");
  }
  ASSERT(pass != nullptr);
  pass->_pass_parse(cnf);
  return pass;
}

bool passes_parse(CompConf& conf, toml::table& tbl) {
  for (auto&& [name, data] : tbl) {
    auto str_name = name.str();
    auto end = conf.passes.end();
    for (auto i = conf.passes.begin(); i != end; ++i) {
      auto pass_ptr = *(*i).get_raw_ptr();
      if (pass_ptr->get_name() == str_name) {
        if (!pass_ptr->_pass_parse(&data)) {
          return false;
        }
      }
    }
    auto p = setup_pass(name.str(), data.as_table());
    if (!p) {
      return false;
    }
    conf.passes.push_back(p.value());
  }
  return true;
}

bool pipeline_parse(PipelineRef pipeline, CompConf& conf,
                    toml::node_view<toml::node> cnf) {
  if (cnf["fir_passes"].is_array()) {
    pipeline->fir_passes.clear();
    for (auto&& pass_name_val : *cnf["fir_passes"].as_array()) {
      auto maybe_pass_name = pass_name_val.value<std::string_view>();
      ASSERT(maybe_pass_name.has_value());
      auto pass_name = maybe_pass_name.value();
      if (pass_name.starts_with("pipeline:")) {
        auto real_name = pass_name.substr(9);
        auto found_pass = conf.find_pipeline(real_name);
        pipeline->fir_passes.push_back({found_pass});
      } else {
        auto found_pass = conf.find_pass(pass_name);
        pipeline->fir_passes.push_back({found_pass});
      }
    }
  }
  return true;
}

bool pipelines_parse(CompConf& conf, toml::table& tbl) {
  for (auto&& [name, data] : tbl) {
    auto str_name = name.str();
    auto end = conf.pipelines.end();
    for (auto i = conf.pipelines.begin(); i != end; ++i) {
      if (i->name == str_name) {
        if (!pipeline_parse({*i}, conf, toml::node_view(data))) {
          return false;
        }
      }
    }
    Pipeline p;
    p.name = str_name;
    auto ref = conf.pipelines.push_back(p);
    if (!pipeline_parse({ref}, conf, toml::node_view(data))) {
      return false;
    }
  }
  return true;
}

void setup_default(CompConf& conf) { conf.parse("../src/default.toml"); }

bool include_parse(CompConf& conf, toml::array& arr) {
  for (auto&& include : arr) {
    auto maybe_name = include.value<std::string_view>();
    ASSERT(maybe_name.has_value());
    auto name = maybe_name.value();
    if (name == "default") {
      setup_default(conf);
    } else {
      conf.parse(name.begin());
    }
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
  if (tbl["pass"].is_table()) {
    if (!passes_parse(conf, *tbl["pass"].as_table())) {
      return false;
    }
  }
  if (tbl["pipeline"].is_table()) {
    if (!pipelines_parse(conf, *tbl["pipeline"].as_table())) {
      return false;
    }
  }
  optimize_parse(conf.optim, conf, tbl["optimize"]);
  return true;
}
}  // namespace

PipelineRef CompConf::find_pipeline(std::string_view name) {
  for (auto pipe : pipelines) {
    if (pipe->name == name) {
      return {pipe};
    }
  }
  fmt::println("Searched for pipeline with name '{}'", name);
  TODO("Failed to find pipeline with that name");
}

PassRef CompConf::find_pass(std::string_view name) {
  for (auto pass : passes) {
    if (name == (*pass.get_raw_ptr())->get_name()) {
      return {pass};
    }
  }
  fmt::println("Searched for pass with name '{}'", name);
  TODO("Failed to find pass with that name");
}

bool CompConf::parse(const char* filename) {
  using namespace std::literals;

  toml::table tbl;
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
