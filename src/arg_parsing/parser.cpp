#include "parser.hpp"

#include <fmt/base.h>
#include <fmt/core.h>

#include <argparse/argparse.hpp>
#include <cassert>
#include <string_view>

#include "config/compiler_config.hpp"
#include "utils/tracy.hpp"

void parse_args(int argc, char *argv[], foptim::conf::CompConf &conf) {
  ZoneScopedN("Arg Parsing");
  std::string config = "Default";

  argparse::ArgumentParser program("foptim");
  program.add_argument("--workers")
      .help("N Workers")
      .scan<'i', int>()
      .default_value(0);
  program.add_argument("--verbosity")
      .help("verbosity")
      .scan<'i', int>()
      .default_value(254);
  program.add_argument("--print-mir")
      .help("print MIR instead of outputing an object")
      .flag();
  program.add_argument("--no-reorder-funcs")
      .help("keep functions in input order (deterministic output order)")
      .flag();
  program.add_argument("--cconffile")
      .store_into(config)
      .default_value("Default")
      .help("where the cconf.toml is located at")
      .default_value("default");
  program.add_argument("input").default_value("-").help("specify the input .ll file.");
  program.add_argument("output").default_value("/dev/null").help(
      "specify the output .ss file.");

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception &err) {
    fmt::println("{}", err.what());
    std::exit(1);
  }

  fmt::println("Using config '{}'", config);
  ASSERT(conf.parse(config));

  conf.number_worker_threads = program.get<int>("workers");
  assert(conf.number_worker_threads >= 0 && conf.number_worker_threads <= 8 &&
         "Invalid number of worker threads");
  conf.debug.verbosity = static_cast<foptim::u8>(program.get<int>("verbosity"));
  conf.input.in_file = program.get<std::string>("input");
  conf.output.out_file = program.get<std::string>("output");
  if (program["--no-reorder-funcs"] == true) {
    conf.debug.no_reorder_funcs = true;
  }
  if (program["--print-mir"] == true) {
    conf.output.type = foptim::conf::Output::OutputType::PrintMIR;
  }
}
