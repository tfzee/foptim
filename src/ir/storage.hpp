#pragma once
#include "../utils/stable_vec.hpp"
#include "basic_block.hpp"
#include "function.hpp"
#include "instruction_data.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/global.hpp"
#include "types.hpp"
#include "utils/map.hpp"
#include "utils/vec.hpp"
#include "utils/string.hpp"

namespace foptim::fir {

#define create_storage(Ty, name)            \
  utils::StableVec<Ty> storage_##name = {}; \
  Ty##R insert_##name(Ty v) { return Ty##R(this->storage_##name.push_back(v)); }

// Owns all functions. The map gives name lookup, the vec keeps insertion order
// so iteration is deterministic. Always go through the helpers so both stay in
// sync.
class FunctionStorage {
  IRMap<IRString, std::unique_ptr<Function>> map;
  IRVec<Function*> order;

 public:
  Function* insert(IRString name, std::unique_ptr<Function> func) {
    auto* raw = func.get();
    auto [it, inserted] = map.emplace(std::move(name), std::move(func));
    ASSERT_M(inserted, "Function with this name already exists");
    order.push_back(raw);
    return raw;
  }

  bool contains(IRStringRef name) const { return map.contains(name); }

  // nullptr if it does not exist
  Function* get(IRStringRef name) const {
    auto it = map.find(name);
    return it == map.end() ? nullptr : it->second.get();
  }

  bool erase(IRStringRef name) {
    auto it = map.find(name);
    if (it == map.end()) {
      return false;
    }
    std::erase(order, it->second.get());
    map.erase(it);
    return true;
  }

  // keeps the position in the iteration order
  void rename(IRStringRef old_name, IRString new_name) {
    auto it = map.find(old_name);
    ASSERT(it != map.end());
    ASSERT(!map.contains(new_name.c_str()));
    auto func = std::move(it->second);
    map.erase(it);
    func->name = new_name;
    map.emplace(std::move(new_name), std::move(func));
  }

  size_t size() const { return order.size(); }
  // insertion order. Copy before iterating if the loop body may insert/erase
  const IRVec<Function*>& all() const { return order; }
};

class IRStorage {
 public:
  FunctionStorage functions;
  utils::StableVec<std::unique_ptr<GlobalData>> storage_global;
  utils::StableVec<InstrData> storage_instr;
  utils::StableVec<BasicBlockData> basic_blocks;
  utils::StableVec<BBArgumentData> bb_args;
  utils::StableVec<ConstantValue> storage_constant;
  utils::StableVec<AnyType> storage_type;

  BBArgument insert_bb_arg(BasicBlock bb, TypeR t) {
    return BBArgument(this->bb_args.push_back({bb, t}));
  }

  template <typename T>
  Global insert_global(T&& v) {
    return Global(this->storage_global.push_back(std::forward<T>(v)));
  }

  Global insert_global(std::unique_ptr<GlobalData> v) {
    return Global(this->storage_global.push_back(std::move(v)));
  }

  template <typename T>
  Instr insert_instr(T&& v) {
    return Instr(this->storage_instr.push_back(std::forward<T>(v)));
  }

  template <typename T>
  BasicBlock insert_bb(T&& v) {
    return BasicBlock(this->basic_blocks.push_back(std::forward<T>(v)));
  }

  template <typename T>
  BBArgument insert_bb_arg(T&& v) {
    return BBArgument(this->bb_args.push_back(std::forward<T>(v)));
  }

  template <typename T>
  ConstantValueR insert_constant(T&& v) {
    return ConstantValueR(this->storage_constant.push_back(std::forward<T>(v)));
  }

  template <typename T>
  TypeR insert_type(T&& v) {
    return TypeR(this->storage_type.push_back(std::forward<T>(v)));
  }
};

}  // namespace foptim::fir
