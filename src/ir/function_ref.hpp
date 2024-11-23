#pragma once

namespace foptim::fir {
class Builder;
class Function;

class FunctionR {
public:
  Function *func;
  FunctionR(Function *func) : func(func) {}

  Function *operator->() const { return func; }

  bool operator==(FunctionR other) const{
    return (void*)func == other.func;
  } 
  Builder builder();
};

using CFunctionR = const FunctionR;
} // namespace foptim::fir
