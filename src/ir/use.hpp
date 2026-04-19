#pragma once
#include "../utils/types.hpp"
#include "instruction.hpp"
#include "utils/mutex.hpp"
#include "utils/vec.hpp"

namespace foptim::fir {

enum class UseType : u8 {
  NormalArg,
  BB,
  BBArg,
};

class Use {
 private:
  constexpr Use(Instr user, UseType type, u16 argId, u16 bbArgId)
      : user(user), type(type), argId(argId), bbArgId(bbArgId) {}

 public:
  constexpr Use(Use &&) noexcept = default;
  constexpr Use &operator=(Use &&) noexcept = default;
  constexpr Use(const Use &) noexcept = default;
  constexpr Use &operator=(const Use &) noexcept = default;

  Instr user;
  UseType type;
  u16 argId;
  u16 bbArgId;  // always 0 otherwise

  static constexpr Use norm(Instr r, u16 argId) {
    return {r, UseType::NormalArg, argId, 0};
  }
  static constexpr Use bb(Instr r, u16 bb_id) {
    return {r, UseType::BB, bb_id, 0};
  }
  static constexpr Use bb_arg(Instr r, u16 bb_id, u16 bb_argid) {
    return {r, UseType::BBArg, bb_id, bb_argid};
  }

  TypeR get_type();
  ValueR get_value();
  void replace_use(ValueR new_value);

  [[nodiscard]] bool operator==(const Use &other) const;
};

class Used {
 public:
  IRVec<Use> uses;

  void add_usage(Use u) { uses.push_back(u); }
  [[nodiscard]] size_t get_n_uses() const { return uses.size(); }
  [[nodiscard]] const IRVec<Use> &get_uses() const { return uses; }

  void replace_all_uses(ValueR new_value);
  void remove_usage(const Use &use, bool verify);
  void remove_all_usages();
};

class LockedUsed {
 public:
  Mutex<Used> _uses;

  void add_usage(Use u) {
    auto us = _uses.scoped_lock();
    us->uses.push_back(u);
  }
  [[nodiscard]] size_t get_n_uses() const {
    auto us = _uses.scoped_lock();
    return us->uses.size();
  }
  [[nodiscard]] const IRVec<Use> &get_uses() const {
    auto us = _uses.scoped_lock();
    return us->uses;
  }

  void replace_all_uses(ValueR new_value);
  void remove_usage(const Use &use, bool verify);
  void remove_all_usages();
};

}  // namespace foptim::fir

template <>
struct std::hash<foptim::fir::Use> {
  std::size_t operator()(const foptim::fir::Use &k) const {
    using foptim::u16;
    using foptim::u8;
    using foptim::fir::Instr;
    using std::hash;

    return hash<Instr>()(k.user) ^ hash<u8>()((u8)k.type) ^
           hash<u16>()(k.argId) ^ hash<u16>()(k.bbArgId);
  }
};
