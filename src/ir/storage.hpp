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
#include "utils/string.hpp"

namespace foptim::fir {

#define create_storage(Ty, name)            \
  utils::StableVec<Ty> storage_##name = {}; \
  Ty##R insert_##name(Ty v) { return Ty##R(this->storage_##name.push_back(v)); }

class IRStorage {
 public:
  IRMap<IRString, std::unique_ptr<Function>> functions;
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
