#pragma once
#include "../utils/types.hpp"
#include "instruction.hpp"
#include "utils/vec.hpp"

namespace foptim::fir {

enum class UseType {
  NormalArg,
  BB,
  BBArg,
};

class Use {
private:
  constexpr Use(Instr user, UseType type, u16 argId, u16 bbArgId)
      : user(user), type(type), argId(argId), bbArgId(bbArgId) {}

public:
  Instr user;
  UseType type;
  u16 argId;
  u16 bbArgId;//always 0 otherwise

  static constexpr Use norm(Instr r, u16 argId) {
    return {r, UseType::NormalArg, argId, 0};
  }
  static constexpr Use bb(Instr r, u16 bb_id) { return {r, UseType::BB, bb_id, 0}; }
  static constexpr Use bb_arg(Instr r, u16 bb_id, u16 bb_argid) {
    return {r, UseType::BBArg, bb_id, bb_argid};
  }
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

} // namespace foptim::fir
