#pragma once
#include <fmt/core.h>

#include <unordered_map>

#include "types.hpp"
#include "utils/todo.hpp"

namespace foptim::utils {

class StatCollector {
 public:
  enum StatType {
    STAT_ANY,
    StatFOptim,
    StatMatcher,
    StatMirOptim,
    StatOther,
  };
  enum class StatValType {
    I64,
    F64,
  };
  struct Stat {
    StatValType type;
    StatType ty;
    union {
      i64 iv;
      f64 dv;
    };
  };
  std::unordered_map<const char *, Stat> stats;

  static StatCollector &get() {
    static StatCollector coll;
    return coll;
  }

  void seti(i64 v, const char *name, StatType ty = StatOther) {
    if (stats.contains(name)) {
      ASSERT(stats[name].ty == ty);
      ASSERT(stats[name].type == StatValType::I64);
    } else {
      stats[name].type = StatValType::I64;
      stats[name].ty = ty;
    }
    stats[name].iv = v;
  }
  void setf(f64 v, const char *name, StatType ty = StatOther) {
    if (stats.contains(name)) {
      ASSERT(stats[name].ty == ty);
      ASSERT(stats[name].type == StatValType::F64);
    } else {
      stats[name].type = StatValType::I64;
      stats[name].ty = ty;
    }
    stats[name].dv = v;
  }
  void addi(i64 v, const char *name, StatType ty = StatOther) {
    if (stats.contains(name)) {
      ASSERT(stats[name].ty == ty);
      ASSERT(stats[name].type == StatValType::I64);
    } else {
      stats[name].type = StatValType::I64;
      stats[name].ty = ty;
      stats[name].iv = 0;
    }
    stats[name].iv += v;
  }
  void addf(f64 v, const char *name, StatType ty = StatOther) {
    if (stats.contains(name)) {
      ASSERT(stats[name].ty == ty);
      ASSERT(stats[name].type == StatValType::F64);
    } else {
      stats[name].type = StatValType::I64;
      stats[name].ty = ty;
      stats[name].dv = 0;
    }
    stats[name].dv += v;
  }
  void dump(StatType filter_ty = STAT_ANY) {
    fmt::println("======STATS======");
    fmt::println("     {: <25}: {: >5}", "NStats", stats.size());
    for (const auto &[name, stat] : stats) {
      if (stat.ty == filter_ty || stat.ty == STAT_ANY ||
          filter_ty == STAT_ANY) {
        switch (stat.ty) {
          case STAT_ANY:
            fmt::print("ANYY:");
            break;
          case StatFOptim:
            fmt::print("FOPT:");
            break;
          case StatMatcher:
            fmt::print("MATC:");
            break;
          case StatMirOptim:
            fmt::print("MIRO:");
            break;
          case StatOther:
            fmt::print("OTHR:");
            break;
        }
        switch (stat.type) {
          case StatValType::I64:
            fmt::println("{: <25}: {: >5}", name, stat.iv);
            break;
          case StatValType::F64:
            fmt::println("{: <25}: {: >5}", name, stat.dv);
            break;
        }
      }
    }
  }
};
}  // namespace foptim::utils
