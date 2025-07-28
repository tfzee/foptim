#pragma once
#include <fmt/color.h>
#include <fmt/format.h>

#include "types.hpp"

template <class T>
class BaseIRFormatter {
 public:
  bool debug = false;
  bool color = false;

  constexpr auto parse(auto &ctx) {
    auto it = ctx.begin();
    auto end = ctx.end();

    while (it != end && *it != '}') {
      switch (*it) {
        case 'd':
          debug = true;
          break;
        case 'c':
          color = true;
          break;
        case 'p':
          debug = false;
          break;
        default:
          return it;
      }
      ++it;
    }

    return it;
  }
};

template <class T>
concept IsSRef = requires(T t) {
  t.is_valid();
  t.get_raw_ptr();
};

template <IsSRef T>
class fmt::formatter<T> : public BaseIRFormatter<T> {
 public:
  template <class CTX>
  appender format(const T &k, CTX &ctx) const {
    auto app = ctx.out();
    if (!k.is_valid()) {
      return fmt::format_to(ctx.out(), "INVALID");
    }
    if (this->color && this->debug) {
      return fmt::format_to(app, "{:cd}", *k.get_raw_ptr());
    }
    if (this->color) {
      return fmt::format_to(app, "{:c}", *k.get_raw_ptr());
    }
    if (this->debug) {
      return fmt::format_to(app, "{:d}", *k.get_raw_ptr());
    }
    return fmt::format_to(app, "{}", *k.get_raw_ptr());
  }
};

constexpr auto color_value = fg(fmt::color::light_green);
constexpr auto color_value2 = fg(fmt::color::cornflower_blue);
constexpr auto color_bb = fg(fmt::color::light_blue);
constexpr auto color_debug = fg(fmt::color::brown);
constexpr auto color_func = fg(fmt::color::orange);
constexpr auto color_constant = fg(fmt::color::dark_orange);
constexpr auto color_number = fg(fmt::color::medium_orchid);
constexpr auto color_type = fg(fmt::color::light_coral);

template <>
class fmt::formatter<foptim::optim::KnownBits>
    : public BaseIRFormatter<foptim::optim::KnownBits> {
 public:
  appender format(foptim::optim::KnownBits const &k, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fir::ValueR>
    : public BaseIRFormatter<foptim::fir::ValueR> {
 public:
  appender format(foptim::fir::ValueR const &k, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fir::Attribute>
    : public BaseIRFormatter<foptim::fir::Attribute> {
 public:
  appender format(foptim::fir::Attribute const &attrib,
                  format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fir::Function>
    : public BaseIRFormatter<foptim::fir::Function> {
 public:
  appender format(foptim::fir::Function const &func, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fir::BasicBlock>
    : public BaseIRFormatter<foptim::fir::BasicBlock> {
 public:
  appender format(foptim::fir::BasicBlock const &bb, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fir::BBArgument>
    : public BaseIRFormatter<foptim::fir::BBArgument> {
 public:
  appender format(foptim::fir::BBArgument const &v, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fir::Instr>
    : public BaseIRFormatter<foptim::fir::Instr> {
 public:
  appender format(foptim::fir::Instr const &instr, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fir::Use>
    : public BaseIRFormatter<foptim::fir::Use> {
 public:
  appender format(foptim::fir::Use const &v, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fir::ConstantValueR>
    : public BaseIRFormatter<foptim::fir::ConstantValueR> {
 public:
  appender format(foptim::fir::ConstantValueR const &v,
                  format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fir::ConstantValue>
    : public BaseIRFormatter<foptim::fir::ConstantValue> {
 public:
  appender format(foptim::fir::ConstantValue const &v,
                  format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fir::AnyType>
    : public BaseIRFormatter<foptim::fir::AnyType> {
 public:
  appender format(foptim::fir::AnyType const &v, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fmir::MFunc>
    : public BaseIRFormatter<foptim::fmir::MFunc> {
 public:
  appender format(foptim::fmir::MFunc const &func, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fmir::MInstr>
    : public BaseIRFormatter<foptim::fmir::MInstr> {
 public:
  appender format(foptim::fmir::MInstr const &v, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fmir::VReg>
    : public BaseIRFormatter<foptim::fmir::VReg> {
 public:
  appender format(foptim::fmir::VReg const &v, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fmir::MBB>
    : public BaseIRFormatter<foptim::fmir::MBB> {
 public:
  appender format(foptim::fmir::MBB const &bb, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fmir::MArgument>
    : public BaseIRFormatter<foptim::fmir::MArgument> {
 public:
  appender format(foptim::fmir::MArgument const &v, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fmir::Type>
    : public BaseIRFormatter<foptim::fmir::Type> {
 public:
  appender format(foptim::fmir::Type const &v, format_context &ctx) const;
};

template <>
class fmt::formatter<foptim::fir::BBRefWithArgs>
    : public BaseIRFormatter<foptim::fir::BBRefWithArgs> {
 public:
  appender format(foptim::fir::BBRefWithArgs const &bb_with_args,
                  format_context &ctx) const;
};
