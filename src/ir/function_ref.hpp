#pragma once

namespace foptim::fir {
class Builder;
class Function;

class FunctionR {
public:
  Function *func;
  FunctionR(Function *func) : func(func) {}

  Function *operator->() { return func; }
  const Function *operator->() const { return func; }
  Builder builder();
};
} // namespace foptim::fir
