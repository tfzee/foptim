#include "stack_known_bits.hpp"

namespace foptim::optim {

void StackKnownBits::update_call(fir::Instr instr, utils::BitSet<> &new_in_one,
                                 utils::BitSet<> &new_in_zero,
                                 TMap<fir::ValueR, CachedKnowledge> &cache) {

  if (instr->args[0].is_constant() && instr->args[0].as_constant()->is_func() &&
      (instr->args[0].as_constant()->as_func()->name == "foptim.memset")) {
    auto ptr = instr->args[1];
    auto val = instr->args[2];
    auto size = instr->args[3];
    u64 offset = 0;
    auto result = get_stack_offset(offset, ptr, cache);
    if (result == StackOffsetResult::KnownNonLocal) {
      return;
    }
    if (!val.is_constant()) {
      new_in_one.reset(false);
      new_in_zero.reset(false);
      return;
    }
    auto cval = val.as_constant()->as_int();
    if (cval == 0 && result == StackOffsetResult::UnknownLocal) {
      new_in_one.reset(false);
      return;
    }
    if (cval == 255 && result == StackOffsetResult::UnknownLocal) {
      new_in_zero.reset(false);
      return;
    }
    if (!size.is_constant()) {
      // TODO: can improve this to only clear above the offset
      if (cval == 0) {
        new_in_one.reset(false);
        return;
      }
      if (cval == 255) {
        new_in_zero.reset(false);
        return;
      }
    }

    auto csize = size.as_constant()->as_int();
    ASSERT(result == StackOffsetResult::KnownLocal);

    for (u64 i = 0; i < csize; i++) {
      auto curr_off = offset + i;
      new_in_one.set(curr_off * 8, 8, (u8)cval);
      new_in_zero.set(curr_off * 8, 8, ~(u8)cval);
    }
    return;
  }

  for (auto arg : instr->args) {
    if (arg.get_type()->is_ptr()) {
      u64 offset = 0;
      auto result = get_stack_offset(offset, arg, cache);
      // fmt::println("Result for {} arg: knwn {} unknwn {}", arg,
      //              result == StackOffsetResult::KnownLocal,
      //              result == StackOffsetResult::UnknownLocal);
      if (result == StackOffsetResult::KnownLocal ||
          result == StackOffsetResult::UnknownLocal) {
        new_in_one.reset(false);
        new_in_zero.reset(false);
        return;
      }
    }
  }
}

void StackKnownBits::update_load(fir::Instr instr, utils::BitSet<> &new_in_one,
                                 utils::BitSet<> &new_in_zero,
                                 TMap<fir::ValueR, CachedKnowledge> &cache,
                                 TMap<fir::ValueR, i128> &known_load_values) {
  u64 offset = 0;
  auto result = get_stack_offset(offset, instr->args[0], cache);
  if (result == StackOffsetResult::UnknownLocal) {
    // fmt::println("cant handle! {}", instr);
    return;
  }
  if (result != StackOffsetResult::KnownLocal) {
    return;
  }

  auto load_width = instr->get_type()->get_size() * 8;
  u64 in_zero = new_in_zero.get(offset * 8, load_width);
  u64 in_one = new_in_one.get(offset * 8, load_width);
  ASSERT((in_one & in_zero) == 0);

  u64 known_bits = in_zero | in_one;
  u64 mask = load_width == 64 ? std::bit_cast<u64>((i64)-1)
                              : ((1ULL << load_width) - 1);
  // fmt::println("off: {} value {} mask {}\n", offset, in_one, mask);
  if ((known_bits & mask) != mask) {
    return;
  }
  known_load_values[fir::ValueR(instr)] = in_one;
}

bool StackKnownBits::update_store(fir::Instr instr, utils::BitSet<> &new_in_one,
                                  utils::BitSet<> &new_in_zero,
                                  TMap<fir::ValueR, CachedKnowledge> &cache) {
  u64 offset = 0;
  auto result = get_stack_offset(offset, instr->args[0], cache);
  if (result == StackOffsetResult::KnownNonLocal) {
    return true;
  }

  if (instr->args[1].is_constant() &&
      (instr->args[1].as_constant()->is_int() ||
       instr->args[1].as_constant()->is_float())) {
    u64 value = 0;
    bool is_int = instr->args[1].as_constant()->is_int();
    bool is_float = instr->args[1].as_constant()->is_float();
    if (is_int) {
      value = instr->args[1].as_constant()->as_int();
    } else if (is_float) {
      value = std::bit_cast<u64>(instr->args[1].as_constant()->as_float());
    } else {
      UNREACH();
    }
    if (result == StackOffsetResult::KnownLocal) {
      auto size = instr->get_type()->get_size() * 8;
      u64 mask = 0;
      if (size == 64) {
        mask = ~mask;
      } else {
        mask = ((1ULL << size) - 1);
      }
      // fmt::println("{}", size);
      // fmt::println("Got constant store at {} with size {} and mask {}",
      // offset, size, mask);
      new_in_one.set(offset * 8, size, value & mask);
      new_in_zero.set(offset * 8, size, (~value) & mask);
    } else if (result == StackOffsetResult::UnknownLocal && value == 0 &&
               is_int) {
      // need to handle float seperatly because of -0
      new_in_one.reset(false);
    } else if (result == StackOffsetResult::UnknownLocal) {
      // TODO theres edgecases to improve this
      new_in_one.reset(false);
      new_in_zero.reset(false);
    }
    return true;
  }

  {
    u64 offset = 0;
    auto result = get_stack_offset(offset, instr->args[1], cache);
    // if we store a local pointer to somwhere we gotta cancel hard to track
    if (result == StackOffsetResult::KnownNonLocal ||
        result == StackOffsetResult::UnknownLocal) {
      return false;
    }
  }

  // we dont know what we write so just reset the bits accordingly
  if (result == StackOffsetResult::KnownLocal) {
    auto size = instr->get_type()->get_size() * 8;
    new_in_one.set(offset * 8, size, 0);
    new_in_zero.set(offset * 8, size, 0);
  }
  if (result == StackOffsetResult::UnknownLocal) {
    new_in_one.reset(false);
    new_in_zero.reset(false);
  }
  return true;
}

StackKnownBits::StackOffsetResult
StackKnownBits::get_stack_offset(u64 &offset, fir::ValueR ptr,
                                 TMap<fir::ValueR, CachedKnowledge> &cache) {
  using namespace foptim::fir;
  offset = 0;
  if (cache.contains(ptr)) {
    auto res = cache.at(ptr);
    offset = res.offset;
    return res.result;
  }

  if (ptr.is_constant()) {
    return StackOffsetResult::KnownNonLocal;
  }
  if (ptr.is_bb_arg()) {
    auto arg_parent = ptr.as_bb_arg()->get_parent();
    auto entry_block = arg_parent->get_parent()->get_entry();
    return arg_parent == entry_block ? StackOffsetResult::KnownNonLocal
                                     : StackOffsetResult::UnknownLocal;
    // return StackOffsetResult::UnknownLocal;
  }
  if (ptr.is_instr()) {
    auto ptr_instr = ptr.as_instr();
    if (ptr_instr->is(fir::InstrType::AllocaInstr)) {
      fmt::println("Cache should have handled this\n");
      UNREACH();
    }
    if (ptr_instr->is(fir::InstrType::LoadInstr)) {
      // u64 sub_offset = 0;
      // auto sub_result = get_stack_offset(sub_offset, ptr_instr->args[0]);
      // if (sub_result == UnknownLocal || sub_result == KnownLocal) {
      // }
      return StackOffsetResult::UnknownLocal;
    }
    if (ptr_instr->is(fir::InstrType::CallInstr)) {
      for (auto arg : ptr_instr->args) {
        if (arg.get_type()->is_ptr()) {
          u64 sub_offset = 0;
          auto sub_result = get_stack_offset(sub_offset, arg, cache);
          if (!cache.contains(arg)) {
            cache.insert({arg, {sub_result, sub_offset}});
          }
          if (sub_result == UnknownLocal || sub_result == KnownLocal) {
            return StackOffsetResult::UnknownLocal;
          }
        }
      }
      return StackOffsetResult::KnownNonLocal;
    }
    if (ptr_instr->is(fir::InstrType::BinaryInstr) &&
        ((BinaryInstrSubType)ptr_instr->subtype ==
         BinaryInstrSubType::IntAdd)) {
      u64 sub_offset = 0;
      auto sub_result = get_stack_offset(sub_offset, ptr_instr->args[0], cache);
      if (!cache.contains(ptr_instr->args[0])) {
        cache.insert({ptr_instr->args[0], {sub_result, sub_offset}});
      }
      if (sub_result == UnknownLocal) {
        return UnknownLocal;
      }
      if (sub_result == KnownLocal) {
        if (ptr_instr->args[1].is_constant() &&
            ptr_instr->args[1].as_constant()->is_int()) {
          auto value = ptr_instr->args[1].as_constant()->as_int();
          offset += sub_offset + value;
          return KnownLocal;
        }
        return UnknownLocal;
      }
      // just assume the worst
      return StackOffsetResult::UnknownLocal;
    }
    if (ptr_instr->is(fir::InstrType::Conversion) &&
        (ConversionSubType)ptr_instr->subtype == ConversionSubType::IntToPtr) {
      return UnknownLocal;
    }
    fmt::println("{}", ptr_instr);
  }
  fmt::println("{}", ptr);
  TODO("impl?");
  return StackOffsetResult::UnknownLocal;
}

} // namespace foptim::optim
