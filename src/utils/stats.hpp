#pragma once
#include <fmt/core.h>

#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "types.hpp"
#include "utils/todo.hpp"

namespace foptim::utils {

class StatCollector {
public:
  enum StatType : u8 {
    STAT_ANY,
    StatFOptim,
    StatMatcher,
    StatMirOptim,
    StatTiming,
    StatOther,
    STAT_TYPE_MAX,
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
  std::mutex access_mutex;
  std::unordered_map<std::string_view, Stat> stats;
  std::vector<const char *> string_storage;

  static StatCollector &get() {
    static StatCollector coll;
    return coll;
  }

  void seti(i64 v, const char *name, StatType ty = StatOther) {
    std::lock_guard<std::mutex> l{access_mutex};
    if (stats.contains(name)) [[likely]] {
      ASSERT(stats[name].ty == ty);
      ASSERT(stats[name].type == StatValType::I64);
    } else {
      stats[name].type = StatValType::I64;
      stats[name].ty = ty;
    }
    stats[name].iv = v;
  }
  void setf(f64 v, const char *name, StatType ty = StatOther) {
    std::lock_guard<std::mutex> l{access_mutex};
    if (stats.contains(name)) [[likely]] {
      ASSERT(stats[name].ty == ty);
      ASSERT(stats[name].type == StatValType::F64);
    } else {
      stats[name].type = StatValType::I64;
      stats[name].ty = ty;
    }
    stats[name].dv = v;
  }
  void addi(i64 v, const char *name, StatType ty = StatOther) {
    addi(v, std::string_view{name}, ty);
  }
  void addi(i64 v, const std::string_view name, StatType ty = StatOther) {
    std::lock_guard<std::mutex> l{access_mutex};
    if (stats.contains(name)) [[likely]] {
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
    std::lock_guard<std::mutex> l{access_mutex};
    if (stats.contains(name)) [[likely]] {
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
    std::lock_guard<std::mutex> l{access_mutex};
    fmt::println("======STATS======");
    fmt::println("     {: <25}: {: >5}", "NStats", stats.size());
    // sort them nicely if we dont filter them
    if (filter_ty == STAT_ANY) {
      for (auto stat_type = STAT_ANY + 1; stat_type < STAT_TYPE_MAX;
           stat_type++) {
        switch (static_cast<StatType>(stat_type)) {
        case STAT_TYPE_MAX:
        case STAT_ANY:
          UNREACH();
        case StatFOptim:
          fmt::println("===Foptim Optim:");
          break;
        case StatMatcher:
          fmt::println("===Matcher:");
          break;
        case StatMirOptim:
          fmt::println("===MIR Optim:");
          break;
        case StatOther:
          fmt::println("===Other:");
          break;
        case StatTiming:
          fmt::println("===Timing:");
          break;
        }
        for (const auto &[name, stat] : stats) {
          if (stat.ty == stat_type) {
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
        fmt::println("\n");
      }
    } else {
      for (const auto &[name, stat] : stats) {
        if (stat.ty == filter_ty || stat.ty == STAT_ANY ||
            filter_ty == STAT_ANY) {
          switch (stat.ty) {
          case STAT_TYPE_MAX:
            fmt::print("INVLD:");
            break;
            UNREACH();
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
          case StatTiming:
            fmt::print("TMNG:");
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
  }
};
} // namespace foptim::utils
