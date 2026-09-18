#pragma once
#include "ir/function.hpp"
#include "utils/todo.hpp"
#include "utils/types.hpp"
#include "utils/vec.hpp"
#include <type_traits>

namespace foptim::optim {

struct AnalysisKey {};
template <class DerivedT> struct AnalysisInfo {
  static AnalysisKey *ID() { return &DerivedT::Key; }
};

struct PreservedAnalysis {
  TVec<AnalysisKey *> preserved;
  // so you dont need to insert them manually
  bool preserve_all;

  static PreservedAnalysis all() {
    return PreservedAnalysis{.preserved = {}, .preserve_all = true};
  }
  static PreservedAnalysis none() {
    return PreservedAnalysis{.preserved = {}, .preserve_all = false};
  }
};

struct AnalysisManager {
  struct FunctionAnalysis {
    bool is_dirty;
    virtual ~FunctionAnalysis() = default;
    virtual void on_register(fir::Function & /*func*/, AnalysisManager & /*man*/) {
      IMPL("implement it");
    }
    virtual void update() { IMPL("implement it"); }
  };

  struct GenericAnalysisData {
    IRMap<fir::Function *, std::unique_ptr<FunctionAnalysis>> funcs;
  };

  IRMap<AnalysisKey *, GenericAnalysisData> analysis;

  AnalysisManager() = default;

  template <class T> T *get_func_anylsis(fir::Function *func) {
    static_assert(std::is_base_of_v<FunctionAnalysis, T>,
                  "Analysis needs to inherit from GenericFunctionAnalysis");
    auto &f = analysis[T::ID()];
    auto &analysis = f->funcs[func];

    if (analysis->is_dirty) {
      analysis->update();
    }
    ASSERT(!analysis->is_dirty);
    return static_cast<T *>(analysis.get());
  }

  void invalidate(fir::Function *func = nullptr, PreservedAnalysis pres = {}) {
    if (pres.preserve_all) {
      return;
    }
    if (pres.preserved.empty()) {
      for (auto &[_, x] : analysis) {
        for (auto &[f, y] : x.funcs) {
          if (func == nullptr || f == func) {
            y->is_dirty = true;
          }
        }
      }
      return;
    }
    for (auto &[k, x] : analysis) {
      if (std::ranges::find(pres.preserved, k) != pres.preserved.end()) {
        continue;
      }
      for (auto &[f, y] : x.funcs) {
        if (func == nullptr || f == func) {
          y->is_dirty = true;
        }
      }
    }
  }
};

}; // namespace foptim::optim
