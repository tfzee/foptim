#include <fmt/core.h>

#include <argparse/argparse.hpp>
#include <cassert>
#include "utils/tracy.hpp"
#include "utils/parameters.hpp"

void parse_args(int argc, char *argv[]) {
  ZoneScopedN("Arg Parsing");
  argparse::ArgumentParser program("foptim");
  program.add_argument("--workers")
      .help("N Workers")
      .scan<'i', int>()
      .default_value(0);
  program.add_argument("--verbosity")
      .help("verbosity")
      .scan<'i', int>()
      .default_value(254);
  program.add_argument("input").required().help("specify the input .ll file.");
  program.add_argument("output").required().help(
      "specify the output .ss file.");

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception &err) {
    fmt::println("{}", err.what());
    std::exit(1);
  }

  foptim::utils::number_worker_threads = program.get<int>("workers");
  assert(foptim::utils::number_worker_threads >= 0 &&
         foptim::utils::number_worker_threads <= 8 &&
         "Invalid number of worker threads");
  foptim::utils::verbosity = (foptim::u8)program.get<int>("verbosity");
  foptim::utils::in_file_path = program.get<std::string>("input");
  foptim::utils::out_file_path = program.get<std::string>("output");
}
