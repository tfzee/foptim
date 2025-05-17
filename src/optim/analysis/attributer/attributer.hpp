#pragma once
#include "ir/context.hpp"
#include "ir/value.hpp"
#include "utils/map.hpp"
#include "utils/vec.hpp"
#include <deque>
#include <typeindex>

namespace foptim::optim {

class AttributeAnalysis;
class AttributerManager;
using Worklist =
    std::deque<AttributeAnalysis *, utils::TempAlloc<AttributeAnalysis *>>;

template <class Int, Int best, Int worst> struct IntegerLattice {
  Int value;

  constexpr IntegerLattice() : value(worst) {}
  constexpr IntegerLattice(Int i) : value(i) {}

  [[nodiscard]] constexpr Int getBest() const { return best; }
  [[nodiscard]] constexpr bool isBest() const { return best == value; }
  [[nodiscard]] constexpr Int getWorst() const { return worst; }
  [[nodiscard]] constexpr bool isWorst() const { return worst == value; }

  constexpr operator Int() const { return value; }
};

template <class T, std::pair<T, T> best, std::pair<T, T> worst>
struct PairLattice {
  std::pair<T, T> value;

  constexpr PairLattice() : value(worst) {}
  // constexpr PairLattice(T i) : value({i, i}) {}

  [[nodiscard]] constexpr std::pair<T, T> getBest() const { return best; }
  [[nodiscard]] constexpr bool isBest() const { return best == value; }
  [[nodiscard]] constexpr std::pair<T, T> getWorst() const { return worst; }
  [[nodiscard]] constexpr bool isWorst() const { return worst == value; }

  constexpr operator T() const { return value; }
};

class AttributeAnalysis {
public:
  enum class Result {
    Changed,
    Fixed,
  };
  fir::ValueR associatedValue;
  bool force_update = false;

  AttributeAnalysis() = default;
  virtual void materialize_impl(fir::Context &) = 0;
  virtual Result update_impl(AttributerManager & /*unused*/,
                             Worklist & /*worklist*/) {
    TODO("IMPL");
  };
  virtual ~AttributeAnalysis() = default;
  Result update(AttributerManager &m);
};

class AttributerManager {
public:
  TMap<std::type_index, TMap<fir::ValueR, AttributeAnalysis *>> _attribs;
  TMap<AttributeAnalysis *, TVec<AttributeAnalysis *>> _inverse_dependencies;
  AttributeAnalysis *_currently_updating = nullptr;

  template <class AAna>
  AAna *get_or_create_analysis(fir::ValueR loc, Worklist *worklist = nullptr) {
    static_assert(std::is_base_of<AttributeAnalysis, AAna>::value,
                  "AAna must inherit AttributeAnalysis");
    ASSERT(loc.is_valid(true) && "must be valid");
    if (!_attribs.contains(typeid(AAna))) {
      _attribs.insert({typeid(AAna), {}});
    }
    if (!_attribs.at(typeid(AAna)).contains(loc)) {
      AAna *analysis = utils::TempAlloc<AAna>{}.allocate(1);
      ASSERT(analysis);
      // ASSERT(((size_t)analysis) % alignof(AAna) == 0);
      // std::construct_at(analysis);
      if (worklist) {
        worklist->push_back(analysis);
      }
      new (analysis) AAna();
      analysis->associatedValue = loc;
      _attribs.at(typeid(AAna)).insert({loc, analysis});
    }
    AAna *analysis = (AAna *)_attribs.at(typeid(AAna)).at(loc);
    if (_currently_updating) {
      _inverse_dependencies[analysis].push_back(_currently_updating);
    } else {
      analysis->force_update = true;
    }
    ASSERT(analysis);
    return (AAna *)(analysis);
  }

  void materialize(fir::Context &ctx) {
    for (auto [_, loc] : _attribs) {
      for (auto [_, att] : loc) {
        att->materialize_impl(ctx);
      }
    }
  }

  void run() {
    ZoneScopedN("Attributer::run");
    Worklist worklist;
    for (auto &[_, loc] : _attribs) {
      for (auto &[_, att] : loc) {
        if (att->force_update) {
          worklist.push_back(att);
          att->force_update = false;
        }
      }
    }
    // cleanup invalid stuff
    for (auto &[a, deps] : _inverse_dependencies) {
      if (!a->associatedValue.is_valid(true)) {
        _inverse_dependencies.erase(a);
        continue;
      }
      for (size_t ip1 = deps.size(); ip1 > 0; ip1--) {
        if (!deps[ip1 - 1]->associatedValue.is_valid(true)) {
          deps.erase(deps.begin() + ip1);
        }
      }
    }
    while (!worklist.empty()) {
      auto *curr_ptr = worklist.front();
      worklist.pop_front();
      _currently_updating = curr_ptr;
      ASSERT(curr_ptr->associatedValue.is_valid(true));
      if (curr_ptr->update_impl(*this, worklist) ==
          AttributeAnalysis::Result::Changed) {
        if (_inverse_dependencies.contains(curr_ptr)) {
          for (auto *dependencie : _inverse_dependencies.at(curr_ptr)) {
            worklist.push_back(dependencie);
          }
        }
        worklist.push_back(curr_ptr);
      }
    }
  }
};

} // namespace foptim::optim
