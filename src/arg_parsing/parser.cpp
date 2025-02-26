#include "utils/parameters.hpp"
#include <argparse/argparse.hpp>
#include <fmt/core.h>
#include <tracy/Tracy.hpp>

void parse_args(int argc, char *argv[]) {

  ZoneScopedN("Arg Parsing");
  argparse::ArgumentParser program("FIR");
  program.add_argument("input").required().help("specify the input .ll file.");
  program.add_argument("output").required().help(
      "specify the output .ss file.");

  try {
    program.parse_args(argc, argv);
  } catch (const std::exception &err) {
    fmt::println("{}", err.what());
    std::exit(1);
  }

  foptim::utils::in_file_path = program.get<std::string>("input");
  foptim::utils::out_file_path = program.get<std::string>("output");
}
