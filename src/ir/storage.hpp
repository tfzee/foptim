#pragma once
#include "../utils/stable_vec.hpp"
#include "basic_block.hpp"
#include "function.hpp"
#include "instruction_data.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/global.hpp"
#include "types.hpp"
#include <string>
#include "utils/map.hpp"

namespace foptim::fir {

#define create_storage(Ty, name)                                               \
  utils::StableVec<Ty> storage_##name = {};                                    \
  Ty##R insert_##name(Ty v) { return Ty##R(this->storage_##name.push_back(v)); }

class IRStorage {
public:
  IRMap<std::string, Function> functions;
  utils::FStableVec<GlobalData> storage_global;
  utils::FStableVec<InstrData> storage_instr;
  utils::FStableVec<BasicBlockData> basic_blocks;
  utils::FStableVec<ConstantValue> storage_constant;
  utils::FStableVec<AnyType> storage_type;

  Instr insert_instr(InstrData v) {
    return Instr(this->storage_instr.push_back(v));
  }

  BasicBlock insert_bb(BasicBlockData v) {
    return BasicBlock(this->basic_blocks.push_back(v));
  }

  ConstantValueR insert_constant(ConstantValue v) {
    return ConstantValueR(this->storage_constant.push_back(v));
  }

  Global insert_global(GlobalData v) {
    return Global(this->storage_global.push_back(v));
  }

  TypeR insert_type(AnyType v) {
    return TypeR(this->storage_type.push_back(v));
  }
};

} // namespace foptim::fir
