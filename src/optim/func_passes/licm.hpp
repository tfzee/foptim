
#pragma once
#include "../function_pass.hpp"
#include "utils/logging.hpp"

namespace foptim::optim {

class LICM final : public FunctionPass {
public:
  void apply(fir::Context & /*unused*/, fir::Function & /*unused*/) override{
    TODO("IMPL");
  }
};

} // namespace foptim::optim
