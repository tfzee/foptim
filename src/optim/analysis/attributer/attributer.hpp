#pragma once
#include "ir/value.hpp"
#include "utils/map.hpp"
#include "utils/vec.hpp"
#include <deque>
#include <typeindex>

namespace foptim::optim {

class AttributerManager;

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

class AttributeAnalysis {
public:
  enum class Result {
    Changed,
    Fixed,
  };
  fir::ValueR associatedValue;

  virtual void materialize_impl() = 0;
  virtual Result update_impl(AttributerManager & /*unused*/) { TODO("IMPL"); };
  virtual ~AttributeAnalysis() = default;
  Result update(AttributerManager &m);
};

class AttributerManager {
public:
  TMap<std::type_index, TMap<fir::ValueR, AttributeAnalysis *>> _attribs;
  TMap<AttributeAnalysis *, TVec<AttributeAnalysis *>> _inverse_dependencies;
  AttributeAnalysis *_currently_updating = nullptr;

  template <class AAna> AAna *get_or_create_analysis(fir::ValueR loc) {
    static_assert(std::is_base_of<AttributeAnalysis, AAna>::value,
                  "AAna must inherit AttributeAnalysis");
    if (!_attribs.contains(typeid(AAna))) {
      _attribs.insert({typeid(AAna), {}});
    }
    if (!_attribs.at(typeid(AAna)).contains(loc)) {
      AAna *analysis = utils::TempAlloc<AAna>{}.allocate(1);
      std::construct_at(analysis);
      analysis->associatedValue = loc;
      _attribs.at(typeid(AAna)).insert({loc, analysis});
    }
    AAna *analysis = (AAna *)_attribs.at(typeid(AAna)).at(loc);
    if (_currently_updating) {
      _inverse_dependencies[analysis].push_back(_currently_updating);
    }
    ASSERT(analysis);
    return (AAna *)(analysis);
  }

  void materialize() {
    for (auto [_, loc] : _attribs) {
      for (auto [_, att] : loc) {
        att->materialize_impl();
      }
    }
  }

  void run() {
    std::deque<AttributeAnalysis *, utils::TempAlloc<AttributeAnalysis *>>
        worklist;
    for (auto &[_, loc] : _attribs) {
      for (auto &[_, att] : loc) {
        worklist.push_back(att);
      }
    }
    while (!worklist.empty()) {
      auto *curr_ptr = worklist.front();
      worklist.pop_front();

      if (curr_ptr->update_impl(*this) == AttributeAnalysis::Result::Changed) {
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
