#pragma once
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "optim/analysis/mem/mem_access.hpp"
#include "optim/analysis/mem/ptr_arg_use.hpp"
#include "utils/set.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

// Escape analysis for a single alloca. derived holds every value that may carry
// its address: the alloca, pointer arithmetic and selects on it and pointers
// loaded back out of its own memory. Storing such a pointer into the alloca
// itself (a string pointing at its inline buffer) is no escape as long as the
// loaded pointers are tracked. Anything not known to be harmless is an escape.
struct AllocaEscapeInfo {
  bool escapes = false;
  // the alloca stores a pointer in its own memory
  bool has_self_ptr = false;
  // all values that may carry the address of the alloca
  TSet<fir::ValueR> derived;
};

struct EscapeOptions {
  // a return of the address as this result is no escape
  i32 returned_as = -1;
  // a block argument that gets the address carries it on instead of escaping
  bool through_block_args = false;
  // a call that passes the address to a nocapture parameter or to free is no
  // escape even when it writes through it. the address is not kept, but other
  // code can no longer assume the memory is untouched, so this is only for
  // questions about who can reach the object
  bool nocapture_calls = false;
  // every call that is given the address is an escape, also one that only
  // reads through it. For objects that code of the module can name (a global),
  // the callee may write them directly while reading through the parameter.
  bool calls_escape = false;
};

namespace {

struct SelfRange {
  bool unknown = false;
  u64 off = 0;
  u64 size = 0;
};

inline SelfRange alloca_access_range(fir::ValueR ptr, fir::ValueR base,
                                     u64 size) {
  auto shape = decompose_ptr(ptr);
  if (!(shape.base == base) || shape.has_terms() || shape.offset < 0) {
    return {.unknown = true};
  }
  return {
      .unknown = false, .off = static_cast<u64>(shape.offset), .size = size};
}

inline bool ranges_overlap(const SelfRange &a, const SelfRange &b) {
  if (a.unknown || b.unknown) {
    return true;
  }
  return a.off < b.off + b.size && b.off < a.off + a.size;
}

}  // namespace

inline AllocaEscapeInfo analyze_alloca_escape(fir::ValueR alloca_val,
                                              const EscapeOptions &opts = {}) {
  AllocaEscapeInfo info;
  auto &derived = info.derived;
  derived.insert(alloca_val);

  struct PendingStore {
    fir::Instr store;
    fir::ValueR ptr_stored;
  };
  TVec<PendingStore> self_stores;
  struct NonPtrLoad {
    fir::ValueR addr;
    u64 size;
  };
  TVec<NonPtrLoad> non_ptr_loads;
  bool memcpy_reads = false;
  bool call_reads = false;

  auto escape = [&]() { info.escapes = true; };

  // loads of pointers are only derived once a pointer is known to be stored
  // into the alloca, which may show up after the load
  bool changed = true;
  while (changed && !info.escapes) {
    changed = false;
    TVec<fir::ValueR> snapshot;
    snapshot.reserve(derived.size());
    for (const auto &k : derived) {
      snapshot.push_back(k);
    }

    auto add = [&](fir::ValueR v) {
      if (!derived.contains(v)) {
        derived.insert(v);
        changed = true;
      }
    };

    for (auto v : snapshot) {
      // the const overload also works for the use list of a global
      for (const auto &use : *std::as_const(v).get_uses()) {
        if (use.type == fir::UseType::BBArg && opts.through_block_args) {
          const auto &target = use.user->bbs[use.argId];
          if (target.args[use.bbArgId] == v) {
            add(fir::ValueR{target.bb->args[use.bbArgId]});
          }
          continue;
        }
        if (use.type != fir::UseType::NormalArg) {
          escape();
          return info;
        }
        auto user = use.user;
        // the use list of a multi result value has the uses of all results
        if (!(user->args[use.argId] == v)) {
          continue;
        }
        switch (user->instr_type) {
          case fir::InstrType::ICmp:
          case fir::InstrType::FCmp:
            // comparing an address does not give access to the memory
            break;
          case fir::InstrType::LoadInstr: {
            auto ty = user->result_type();
            if (ty->is_ptr()) {
              if (info.has_self_ptr) {
                add(user.result());
              }
            } else {
              non_ptr_loads.push_back({.addr = v, .size = ty->get_size()});
            }
            break;
          }
          case fir::InstrType::StoreInstr: {
            if (use.argId == 0) {
              break;
            }
            if (!v.get_type()->is_ptr()) {
              escape();
              return info;
            }
            if (!info.has_self_ptr) {
              info.has_self_ptr = true;
              changed = true;
            }
            self_stores.push_back({.store = user, .ptr_stored = v});
            break;
          }
          case fir::InstrType::Intrinsic: {
            if ((user->is(fir::IntrinsicSubType::Memcpy) ||
                 user->is(fir::IntrinsicSubType::Memmove)) &&
                (use.argId == 0 || use.argId == 1)) {
              if (use.argId == 1) {
                memcpy_reads = true;
              }
              break;
            }
            if (user->is(fir::IntrinsicSubType::Memset) && use.argId == 0) {
              break;
            }
            // only reads the bytes, the result is a plain integer
            if (user->is(fir::IntrinsicSubType::Memcmp) && use.argId <= 1) {
              break;
            }
            escape();
            return info;
          }
          case fir::InstrType::CallInstr: {
            if (opts.calls_escape) {
              escape();
              return info;
            }
            // a callee that neither keeps the address nor writes through it
            // only reads the alloca
            const bool kept_out =
                opts.nocapture_calls && (call_arg_nocapture(user, use.argId) ||
                                         call_is_free(user, use.argId));
            if (!kept_out && !call_arg_nocapture_readonly(user, use.argId)) {
              escape();
              return info;
            }
            call_reads = true;
            break;
          }
          case fir::InstrType::ReturnInstr:
            if (static_cast<i32>(use.argId) != opts.returned_as) {
              escape();
              return info;
            }
            break;
          case fir::InstrType::SelectInstr:
            if (use.argId == 0) {
              escape();
              return info;
            }
            add(user.result());
            break;
          case fir::InstrType::BinaryInstr:
          case fir::InstrType::Conversion:
          case fir::InstrType::ITrunc:
          case fir::InstrType::ZExt:
          case fir::InstrType::SExt:
            add(user.result());
            break;
          default:
            escape();
            return info;
        }
      }
    }
  }

  // every pointer stored into memory has to go into the alloca itself
  for (auto &s : self_stores) {
    auto dest = s.store->args[0];
    if (!derived.contains(dest) || !dest.get_type()->is_ptr()) {
      escape();
      return info;
    }
  }
  if (info.has_self_ptr) {
    // copying the memory out would copy the pointers out
    if (memcpy_reads) {
      escape();
      return info;
    }
    // the callee could read a pointer to the alloca out of its memory and
    // keep or write through it, nothing in its parameter says it does not
    if (call_reads) {
      escape();
      return info;
    }
    // reading the pointers as integers would hide them from da tracking
    constexpr u64 ptr_size = 8;
    for (auto &l : non_ptr_loads) {
      auto lr = alloca_access_range(l.addr, alloca_val, l.size);
      for (auto &s : self_stores) {
        auto sr = alloca_access_range(s.store->args[0], alloca_val, ptr_size);
        if (ranges_overlap(lr, sr)) {
          escape();
          return info;
        }
      }
    }
  }
  return info;
}

}  // namespace foptim::optim
