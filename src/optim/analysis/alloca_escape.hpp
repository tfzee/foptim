#pragma once
#include "ir/function.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/use.hpp"
#include "ir/value.hpp"
#include "utils/set.hpp"
#include "utils/vec.hpp"

namespace foptim::optim {

// Escape analysis for a single alloca.
//
// The set `derived` contains every value that may hold the address
// of the alloca the alloca itself, pointer arithmetic on it, selects and
// pointers that were loaded back out of the allocas own memory.
//
// A pointer into the alloca that is stored into the allocas own memory
// (like some stuff like std::string storing a pointer to its inline buffer)
// does not make the alloca escape as long as the pointers read back from it are
// tracked as derived values. Everything that is not explicitly known to be
// harmless counts as an escape.
struct AllocaEscapeInfo {
  bool escapes = false;
  // the alloca stores a pointer in its own memory
  bool has_self_ptr = false;
  // all values that may carry the address of the alloca
  TSet<fir::ValueR> derived;
};

namespace {

struct SelfRange {
  bool unknown = false;
  u64 off = 0;
  u64 size = 0;
};

inline SelfRange alloca_access_range(fir::ValueR ptr, fir::ValueR base,
                                     u64 size) {
  u64 off = 0;
  while (!(ptr == base)) {
    if (!ptr.is_instr()) {
      return {.unknown = true};
    }
    auto instr = ptr.as_instr();
    if (!(instr->is(fir::BinaryInstrSubType::PtrAdd) ||
          instr->is(fir::BinaryInstrSubType::IntAdd)) ||
        !instr->args[1].is_constant_int()) {
      return {.unknown = true};
    }
    off += static_cast<u64>(instr->args[1].as_constant()->as_int());
    ptr = instr->args[0];
  }
  return {.unknown = false, .off = off, .size = size};
}

inline bool ranges_overlap(const SelfRange &a, const SelfRange &b) {
  if (a.unknown || b.unknown) {
    return true;
  }
  return a.off < b.off + b.size && b.off < a.off + a.size;
}

} // namespace

inline AllocaEscapeInfo analyze_alloca_escape(fir::ValueR alloca_val) {
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

  auto escape = [&]() { info.escapes = true; };

  // standard Fixpoint shit
  // loads of pointers are only derived once we know that a pointer
  // is stored into the alloca, which we might find after the load.
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
      for (auto &use : *v.get_uses()) {
        if (use.type != fir::UseType::NormalArg) {
          escape();
          return info;
        }
        auto user = use.user;
        switch (user->instr_type) {
        case fir::InstrType::ICmp:
        case fir::InstrType::FCmp:
          // comparing an address does not give access to the memory
          break;
        case fir::InstrType::LoadInstr: {
          auto ty = user->get_type();
          if (ty->is_ptr()) {
            if (info.has_self_ptr) {
              add(fir::ValueR{user});
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
        case fir::InstrType::CallInstr: {
          if (!user->args[0].is_constant_func()) {
            escape();
            return info;
          }
          auto name = user->args[0].as_constant()->as_func()->name;
          if (name == "foptim.memcpy" && (use.argId == 1 || use.argId == 2)) {
            if (use.argId == 2) {
              memcpy_reads = true;
            }
            break;
          }
          if (name == "foptim.memset" && use.argId == 1) {
            break;
          }
          escape();
          return info;
        }
        case fir::InstrType::SelectInstr:
          if (use.argId == 0) {
            escape();
            return info;
          }
          add(fir::ValueR{user});
          break;
        case fir::InstrType::BinaryInstr:
        case fir::InstrType::Conversion:
        case fir::InstrType::ITrunc:
        case fir::InstrType::ZExt:
        case fir::InstrType::SExt:
          add(fir::ValueR{user});
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

} // namespace foptim::optim
