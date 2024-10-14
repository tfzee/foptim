#pragma once
#include "types.hpp"
#include "utils/vec.hpp"

namespace foptim::utils {
struct BitSet;
}
namespace foptim::fmir {
class VReg;
class MArgument;
class MInstr;
class MBB;
class MFunc;
enum class Type : u16;
} // namespace foptim::fmir
namespace foptim::fir {
class Function;
class FunctionR;
class ValueR;
class BasicBlock;
class Instr;
class InstrData;
struct BBRefWithArgs;
class TypeR;
struct ConstantValue;
class ConstantValueR;
class Attribute;
class Use;
} // namespace foptim::fir

namespace foptim::optim {
struct LiveRange;
}
namespace foptim::utils {

class Printer {
public:
  struct PaddingT {};

  enum class LogLevel : u8 {
    Debug,
    Info,
    Warn,
    Err,
  };

  u8 indent;
  LogLevel level;

  template <class T> Printer operator<<(const FVec<T> data) const {
    *this << "[";
    for (const T &elem : data) {
      *this << elem << ", ";
    }
    return *this << "]";
  }
  template <class T> Printer operator<<(const TVec<T> data) const {
    *this << "[";
    for (const T &elem : data) {
      *this << elem << ", ";
    }
    return *this << "]";
  }

  Printer operator<<(foptim::fir::FunctionR func) const;
  Printer operator<<(const foptim::fir::Function &func) const;

  Printer operator<<(i64 val) const;
  Printer operator<<(i32 val) const;
  Printer operator<<(i16 val) const;
  Printer operator<<(i8 val) const;
  Printer operator<<(u64 val) const;
  Printer operator<<(u32 val) const;
  Printer operator<<(u16 val) const;
  Printer operator<<(u8 val) const;
  Printer operator<<(bool val) const;
  Printer operator<<(f32 val) const;
  Printer operator<<(f64 val) const;
  Printer operator<<(const foptim::utils::BitSet &func) const;
  Printer operator<<(const foptim::optim::LiveRange &live) const;
  // template <class T>
  // Printer operator<<(const std::unordered_set<T> sett) const {
  //   *this << "{";
  //   for (const T &elem : sett) {
  //     *this << elem << ", ";
  //   }
  //   return *this << "}";
  // }
  Printer operator<<(const char *func) const;
  Printer operator<<(const foptim::fir::ConstantValue &v) const;
  Printer operator<<(const foptim::fir::ConstantValueR v) const;
  Printer operator<<(const foptim::fir::BBRefWithArgs &bb_with_args) const;
  Printer operator<<(const foptim::fir::TypeR ty) const;
  Printer operator<<(const foptim::fir::Attribute &ty) const;
  Printer operator<<(const foptim::fir::ValueR val) const;
  Printer operator<<(const foptim::fir::Use &use) const;
  Printer operator<<(const foptim::fmir::MArgument &) const;
  Printer operator<<(const foptim::fmir::Type &) const;
  Printer operator<<(const foptim::fmir::MInstr &) const;
  Printer operator<<(const foptim::fmir::MBB &) const;
  Printer operator<<(const foptim::fmir::MFunc &) const;
  Printer operator<<(const foptim::fmir::VReg &) const;
  Printer operator<<(const foptim::fir::Instr instr) const;
  Printer operator<<(const foptim::fir::InstrData *instr) const;
  Printer operator<<(const foptim::fir::BasicBlock bb) const;
  Printer operator<<(const PaddingT pad) const;
  Printer operator<<(const void *v) const;

  void nl() const;
  [[nodiscard]] constexpr Printer pad(i8 delta) const {
    return Printer{
        level,
        (u8)(indent + delta),
    };
  }

  constexpr Printer(LogLevel ty, u8 indent = 0) : indent(indent), level(ty) {}
};

constexpr Printer::PaddingT padding() { return Printer::PaddingT{}; }

constexpr auto Debug = Printer(Printer::LogLevel::Debug, 0);
constexpr auto Warn = Printer(Printer::LogLevel::Warn, 0);

inline Printer::LogLevel g_log_level = Printer::LogLevel::Debug;

} // namespace foptim::utils
