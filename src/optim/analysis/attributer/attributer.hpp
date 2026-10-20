#pragma once
#include <cstddef>
#include <deque>
#include <memory>
#include <typeindex>
#include <vector>

#include "ir/context.hpp"
#include "ir/value.hpp"
#include "utils/arena.hpp"
#include "utils/map.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {
struct AttributeKey {};
template <class DerivedT>
struct AttributeInfo {
  static AttributeKey *ID() { return &DerivedT::Key; }
};

class AttributeAnalysis;
class AttributerManager;
// FIFO on a vector that keeps its storage (a TempAlloc deque never gives its
// blocks back).
class Worklist {
  std::vector<AttributeAnalysis *> items;
  size_t head = 0;

 public:
  [[nodiscard]] bool empty() const { return head == items.size(); }
  void push_back(AttributeAnalysis *a) { items.push_back(a); }
  AttributeAnalysis *front() const { return items[head]; }
  void pop_front() {
    head++;
    if (head == items.size()) {
      clear();
    }
  }
  void clear() {
    items.clear();
    head = 0;
  }
};

template <class Int, Int best, Int worst>
struct IntegerLattice {
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
  // already in the worklist, an analysis is queued at most once
  bool queued = false;

  AttributeAnalysis() = default;
  // called once associatedValue is set, before the first update
  virtual void init() {}
  // returns whether it changed the IR
  virtual bool materialize_impl(fir::Context &, const AttributerManager &m) = 0;
  virtual Result update_impl(AttributerManager & /*unused*/,
                             Worklist & /*worklist*/) {
    TODO("IMPL");
  };
  virtual ~AttributeAnalysis() = default;
  Result update(AttributerManager &m);
};

class AttributerManager {
 public:
  // Set once run() materialized something that changed the IR (e.g. nsw/nuw on
  // an add). run() materializes everything the manager holds, so this changes
  // instructions other than the one the caller asked about, even if the caller
  // then changes nothing itself. reset() keeps it, a pass using the manager has
  // to look at it (or the result of run) to know whether it changed anything.
  bool ir_modified = false;

  TMap<AttributeKey *, TMap<fir::ValueR, AttributeAnalysis *>> _attribs;
  // TMap<std::type_index, TMap<fir::ValueR, AttributeAnalysis *>> _attribs;
  // analysis -> head of its list of dependents in _dep_nodes
  TMap<AttributeAnalysis *, u32> _inverse_dependencies;
  struct DepNode {
    AttributeAnalysis *dependent;
    u32 next;
  };
  TVec<DepNode> _dep_nodes;
  AttributeAnalysis *_currently_updating = nullptr;
  Worklist _worklist;

  // The temp arena is only released between passes, so analyses live in chunks
  // that reset() reuses.
  static constexpr size_t ChunkSize = 1 << 20;
  std::vector<std::unique_ptr<std::byte[]>> _chunks;
  size_t _chunk = 0;
  size_t _chunk_off = 0;

  void *pool_allocate(size_t size, size_t align) {
    ASSERT(size + align <= ChunkSize);
    while (true) {
      if (_chunk == _chunks.size()) {
        _chunks.push_back(std::make_unique<std::byte[]>(ChunkSize));
      }
      const size_t base = reinterpret_cast<size_t>(_chunks[_chunk].get());
      const size_t aligned = (base + _chunk_off + align - 1) & ~(align - 1);
      const size_t off = aligned - base;
      if (off + size <= ChunkSize) {
        _chunk_off = off + size;
        return _chunks[_chunk].get() + off;
      }
      _chunk++;
      _chunk_off = 0;
    }
  }

  void reset() {
    // keep the storage of the maps, only the entries are stale
    for (auto &[_, per_value] : _attribs) {
      per_value.clear();
    }
    _inverse_dependencies.clear();
    _dep_nodes.clear();
    _chunk = 0;
    _chunk_off = 0;
  }

  template <class AAna>
  const AAna *get_analysis(fir::ValueR loc) const {
    static_assert(std::is_base_of_v<AttributeAnalysis, AAna>,
                  "AAna must inherit AttributeAnalysis");
    // ASSERT(loc.is_valid(true) && "must be valid");
    auto aa_typeid = AAna::ID();
    if (!_attribs.contains(aa_typeid)) {
      return nullptr;
    }
    if (!_attribs.at(aa_typeid).contains(loc)) {
      return nullptr;
    }
    auto analysis =
        std::launder(reinterpret_cast<AAna *>(_attribs.at(aa_typeid).at(loc)));
    return analysis;
  }

  template <class AAna>
  AAna *get_or_create_analysis(fir::ValueR loc, Worklist *worklist = nullptr) {
    static_assert(std::is_base_of_v<AttributeAnalysis, AAna>,
                  "AAna must inherit AttributeAnalysis");
    // ASSERT(loc.is_valid(true) && "must be valid");
    auto aa_typeid = AAna::ID();
    if (!_attribs.contains(aa_typeid)) {
      _attribs.insert({aa_typeid, {}});
    }
    AAna *analysis = nullptr;
    if (!_attribs.at(aa_typeid).contains(loc)) {
      analysis = static_cast<AAna *>(pool_allocate(sizeof(AAna), alignof(AAna)));
      ASSERT(analysis);
      new (analysis) AAna();
      if (worklist) {
        worklist->push_back(analysis);
        analysis->queued = true;
      }
      analysis->associatedValue = loc;
      analysis->init();
      _attribs.at(aa_typeid).insert({loc, analysis});
    } else {
      analysis = std::launder(
          reinterpret_cast<AAna *>(_attribs.at(aa_typeid).at(loc)));
    }
    if (_currently_updating) {
      auto [it, inserted] = _inverse_dependencies.try_emplace(analysis, ~0U);
      _dep_nodes.push_back({_currently_updating, it->second});
      it->second = static_cast<u32>(_dep_nodes.size() - 1);
    } else {
      analysis->force_update = true;
    }
    ASSERT(analysis);
    return analysis;
  }

  // returns whether the IR changed
  bool materialize(fir::Context &ctx) {
    bool modified = false;
    for (auto &[_, loc] : _attribs) {
      for (auto &[_, att] : loc) {
        modified |= att->materialize_impl(ctx, *this);
      }
    }
    return modified;
  }

  // returns whether materializing changed the IR (also kept in ir_modified)
  bool run(fir::Context &ctx) {
    ZoneScopedNC("Attributer::run", COLOR_ANALY);
    Worklist &worklist = _worklist;
    worklist.clear();
    for (auto &[_, loc] : _attribs) {
      for (auto &[_, att] : loc) {
        if (att->force_update) {
          if (!att->queued) {
            worklist.push_back(att);
            att->queued = true;
          }
          att->force_update = false;
        }
      }
    }
    while (!worklist.empty()) {
      auto *curr_ptr = worklist.front();
      worklist.pop_front();
      curr_ptr->queued = false;
      _currently_updating = curr_ptr;
      ASSERT(curr_ptr->associatedValue.is_valid(true));
      if (curr_ptr->update_impl(*this, worklist) ==
          AttributeAnalysis::Result::Changed) {
        if (auto it = _inverse_dependencies.find(curr_ptr);
            it != _inverse_dependencies.end()) {
          for (u32 n = it->second; n != ~0U; n = _dep_nodes[n].next) {
            auto *dependencie = _dep_nodes[n].dependent;
            if (!dependencie->associatedValue.is_valid(true)) {
              continue;
            }
            if (!dependencie->queued) {
              worklist.push_back(dependencie);
              dependencie->queued = true;
            }
          }
        }
        if (!curr_ptr->queued) {
          worklist.push_back(curr_ptr);
          curr_ptr->queued = true;
        }
      }
    }
    _currently_updating = nullptr;
    const bool modified = materialize(ctx);
    ir_modified |= modified;
    return modified;
  }
};

}  // namespace foptim::optim
