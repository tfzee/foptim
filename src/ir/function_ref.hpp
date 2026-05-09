#pragma once

namespace foptim::fir {
class Builder;
class Function;

class FunctionR {
 public:
  Function *func;
  constexpr FunctionR(Function *func) : func(func) {}

  constexpr Function *operator->() const { return func; }

  constexpr bool operator==(FunctionR other) const {
    return reinterpret_cast<void *>(func) == other.func;
  }
  Builder builder();
};

using CFunctionR = const FunctionR;
}  // namespace foptim::fir
