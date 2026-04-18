#pragma once
#include <functional>

#include "ir/value.hpp"
#include "utils/types.hpp"

namespace foptim::optim {

struct AliasAnalyis {
  enum class AAResult : u8 {
    Alias,
    MightAlias,
    NoAlias,
  };
  using HeapId = u32;

  struct Heap {
    HeapId parent;
    u32 id;
    u32 n_children;
  };
  struct HeapEntry {
    HeapId heap;
    std::optional<u32> offset;
  };

 private:
  TMap<fir::ValueR, HeapEntry> mapping;
  TVec<Heap> heaps;
  //                 Any_h top
  // null | poision | argument         | local
  //                | static,dynamic   |
  //                 Any_h top
  HeapId any_h;
  HeapId null_h;
  HeapId poision_h;
  HeapId argument_h;
  HeapId staticH_h;
  HeapId dynamicH_h;
  HeapId local_stack_h;
  bool ids_need_update = true;

  HeapId createHeap(HeapId parent) {
    ids_need_update = true;
    HeapId new_id = heaps.size() + 1;
    heaps.push_back({.parent = parent, .id = new_id, .n_children = 0});
    if (parent != 0) {
      HeapId current = parent;
      while (current != 0) {
        heaps[current - 1].n_children += 1;
        current = heaps[current - 1].parent;
      }
    }
    return new_id;
  }

  void update_heap_ids() {
    TMap<HeapId, TVec<HeapId>> parent_to_children;
    parent_to_children.reserve(heaps.size());

    for (HeapId i = 0; i < heaps.size(); ++i) {
      HeapId p = heaps[i].parent;
      parent_to_children[p].push_back(i + 1);
    }
    u32 current_id = 0;

    std::function<u32(HeapId)> assign_ids = [&](HeapId node) -> u32 {
      heaps[node - 1].id = current_id++;
      u32 count = 1;
      for (HeapId child : parent_to_children[node]) {
        count += assign_ids(child);
      }
      heaps[node - 1].n_children = count - 1;
      return count;
    };
    assign_ids(1);
    ids_need_update = false;
  }

  bool is_desc_eql(HeapId desc, HeapId parent) {
    if (ids_need_update) {
      update_heap_ids();
    }
    if (parent == 0) {
      return true;
    }
    if (desc == 0) {
      return false;
    }
    return (desc == parent) ||
           (heaps[desc - 1].id > heaps[parent - 1].id &&
            heaps[desc - 1].id <=
                heaps[parent - 1].id + heaps[parent - 1].n_children);
  }

  // gets the smallest heap that includes both of these heaps
  HeapId meet(HeapId a, HeapId b) {
    if (is_desc_eql(a, b)) {
      return b;
    }
    if (is_desc_eql(b, a)) {
      return a;
    }
    return meet(heaps[a - 1].parent, heaps[b - 1].parent);
  }

 public:
  AliasAnalyis() {
    reset();
    // fmt::println("true == {}", is_desc_eql(argument_h, any_h));
    // fmt::println("false == {}", is_desc_eql(argument_h, local_stack_h));
    // fmt::println("false == {}", is_desc_eql(argument_h, dynamicH_h));
    // fmt::println("true == {}", is_desc_eql(dynamicH_h, argument_h));
    // fmt::println("true == {}", is_desc_eql(any_h, any_h));
  }

  void reset() {
    heaps.clear();
    mapping.clear();
    any_h = createHeap(0);
    null_h = createHeap(any_h);
    poision_h = createHeap(any_h);
    argument_h = createHeap(any_h);
    staticH_h = createHeap(argument_h);
    dynamicH_h = createHeap(argument_h);
    local_stack_h = createHeap(any_h);
    update_heap_ids();
  }

  HeapEntry analyze(fir::ValueR v) {
    if (mapping.contains(v)) {
      return mapping.at(v);
    }
    auto r = analyze_impl(v);
    mapping.insert({v, r});
    return r;
  }

  HeapEntry analyze_impl(fir::ValueR v);

  bool is_known_local_stack(fir::ValueR v) {
    HeapEntry v_heap = analyze(v);
    return is_desc_eql(v_heap.heap, local_stack_h);
  }

  // checks if the pointer at a/b might overlap given the size
  //  if the size is 0 it assumes they can be any size(useful for example for
  //  arrays and stuff)
  AAResult alias(fir::ValueR a, fir::ValueR b, size_t a_size = 0,
                 size_t b_size = 0) {
    ZoneScopedN("AliasAnalysis");
    if (a == b) {
      return AAResult::Alias;
    }
    HeapEntry a_heap = analyze(a);
    HeapEntry b_heap = analyze(b);
    if (a_heap.heap == 0 || b_heap.heap == 0) {
      return AAResult::MightAlias;
    }
    // TODO: could also handle null_h as special case but idk about stuff like
    // embedded could add a parameter for that case
    if (a_heap.heap == poision_h || b_heap.heap == poision_h) {
      return AAResult::NoAlias;
    }
    if (a_heap.heap == b_heap.heap) {
      if (a_size == 0 || b_size == 0 || !a_heap.offset.has_value() ||
          !b_heap.offset.has_value()) {
        return AAResult::MightAlias;
      }
      bool a_smaller_nooverlap =
          (a_heap.offset.value() + a_size <= b_heap.offset);
      bool b_smaller_nooverlap =
          (b_heap.offset.value() + b_size <= a_heap.offset);
      if (a_smaller_nooverlap || b_smaller_nooverlap) {
        return AAResult::NoAlias;
      }
      return AAResult::MightAlias;
    }
    // TODO: techincally only need to run it if they werent cached
    if (is_desc_eql(b_heap.heap, a_heap.heap) ||
        is_desc_eql(a_heap.heap, b_heap.heap)) {
      return AAResult::MightAlias;
    }
    return AAResult::NoAlias;
  }
};

}  // namespace foptim::optim
