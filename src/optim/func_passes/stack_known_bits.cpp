#include "stack_known_bits.hpp"

#include <fmt/core.h>
#include <llvm/ADT/STLExtras.h>
#include <unistd.h>

#include <deque>
#include <optional>
#include <span>

#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/cfg.hpp"
#include "utils/arena.hpp"

namespace foptim::optim {

// the bytes of an initialized constant global that is not relocated
static std::optional<std::span<const u8>> constant_global_bytes(fir::ValueR v) {
  if (!v.is_constant() || !v.as_constant()->is_global()) {
    return std::nullopt;
  }
  auto g = v.as_constant()->as_global();
  if (!g->is_constant || g->init_value == nullptr || !g->reloc_info.empty()) {
    return std::nullopt;
  }
  return std::span<const u8>{reinterpret_cast<const u8 *>(g->init_value),
                             g->n_bytes};
}

// the bytes a plain load of a constant global with a constant offset reads
static std::optional<std::span<const u8>> constant_load_bytes(fir::ValueR v) {
  if (!v.is_instr() || !v.as_instr()->is(fir::InstrType::LoadInstr) ||
      v.as_instr()->Volatile || v.as_instr()->Ordering != 0) {
    return std::nullopt;
  }
  auto ptr = v.as_instr()->args[0];
  u64 offset = 0;
  if (ptr.is_instr() && ptr.as_instr()->is(fir::BinaryInstrSubType::PtrAdd) &&
      ptr.as_instr()->args[1].is_constant() &&
      ptr.as_instr()->args[1].as_constant()->is_int()) {
    const auto off = ptr.as_instr()->args[1].as_constant()->as_int();
    if (off < 0) {
      return std::nullopt;
    }
    offset = static_cast<u64>(off);
    ptr = ptr.as_instr()->args[0];
  }
  const auto bytes = constant_global_bytes(ptr);
  const u64 size = v.get_type()->get_size();
  if (!bytes || offset + size > bytes->size()) {
    return std::nullopt;
  }
  return bytes->subspan(offset, size);
}

// a memcpy of a constant global into known stack makes those bytes known
static bool copy_constant_bytes(fir::Instr instr, u64 offset,
                                utils::BitSet<> &new_in_one,
                                utils::BitSet<> &new_in_zero) {
  const auto src = constant_global_bytes(instr->args[1]);
  if (!src || !instr->args[2].is_constant() || instr->Volatile) {
    return false;
  }
  const u64 elem_size =
      instr->extra_type.is_valid() ? instr->extra_type->get_size() : 1;
  const u64 n_bytes = instr->args[2].as_constant()->as_int() * elem_size;
  if (n_bytes > src->size() || (offset + n_bytes) * 8 > new_in_one.bit_size()) {
    return false;
  }
  for (u64 i = 0; i < n_bytes; i++) {
    const u8 byte = (*src)[i];
    new_in_one.set((offset + i) * 8, 8, byte);
    new_in_zero.set((offset + i) * 8, 8, static_cast<u8>(~byte));
  }
  return true;
}

static bool is_string_call(fir::Instr instr) {
  if (!instr->is(fir::InstrType::CallInstr) || !instr->args[0].is_constant() ||
      !instr->args[0].as_constant()->is_func()) {
    return false;
  }
  return fir::is_string_libfunc(
      instr->args[0].as_constant()->as_func()->attribs.libfunc);
}

// strlen and strcmp only read, the result is known if both strings are
void StackKnownBits::update_string_call(
    fir::Instr instr, utils::BitSet<> &new_in_one, utils::BitSet<> &new_in_zero,
    StackKnowCache &cache, TMap<fir::Instr, i128> &known_call_values) {
  known_call_values.erase(instr);
  const auto read_string = [&](fir::ValueR ptr) -> std::optional<TVec<u8>> {
    TVec<u8> str;
    if (const auto g = constant_global_bytes(ptr)) {
      for (const u8 c : *g) {
        if (c == 0) {
          return str;
        }
        str.push_back(c);
      }
      return std::nullopt;
    }
    u64 offset = 0;
    const auto result = get_stack_offset(offset, ptr, cache);
    cache.insert({ptr, {.result = result, .offset = offset}});
    if (result != StackOffsetResult::KnownLocal) {
      return std::nullopt;
    }
    for (u64 i = offset;; i++) {
      if ((i + 1) * 8 > new_in_one.bit_size()) {
        return std::nullopt;
      }
      const u64 one = new_in_one.get(i * 8, 8);
      const u64 zero = new_in_zero.get(i * 8, 8);
      if ((one | zero) != 0xFF) {
        return std::nullopt;
      }
      if (one == 0) {
        return str;
      }
      str.push_back(static_cast<u8>(one));
    }
  };
  const bool is_cmp =
      instr->args[0].as_constant()->as_func()->attribs.libfunc ==
      fir::LibFunc::Strcmp;
  if (instr->args.size() != (is_cmp ? 3U : 2U) || !instr->has_one_result() ||
      !instr.result_type()->is_int()) {
    return;
  }
  const auto a = read_string(instr->args[1]);
  const auto b = is_cmp ? read_string(instr->args[2]) : std::nullopt;
  if (!a || (is_cmp && !b)) {
    return;
  }
  i128 res = static_cast<i128>(a->size());
  if (is_cmp) {
    res = 0;
    const size_t n = std::min(a->size(), b->size());
    for (size_t i = 0; i <= n; i++) {
      const int ca = i < a->size() ? (*a)[i] : 0;
      const int cb = i < b->size() ? (*b)[i] : 0;
      if (ca != cb) {
        res = ca - cb;
        break;
      }
    }
  }
  known_call_values[instr] = res;
}

void StackKnownBits::update_mem_intrinsic(fir::Instr instr,
                                          utils::BitSet<> &new_in_one,
                                          utils::BitSet<> &new_in_zero,
                                          StackKnowCache &cache) {
  auto ptr = instr->args[0];
  u64 offset = 0;
  auto result = get_stack_offset(offset, ptr, cache);
  cache.insert({ptr, {.result = result, .offset = offset}});
  if (result == StackOffsetResult::KnownNonLocal) {
    return;
  }
  if (instr->is(fir::IntrinsicSubType::Memcpy) &&
      result == StackOffsetResult::KnownLocal &&
      copy_constant_bytes(instr, offset, new_in_one, new_in_zero)) {
    return;
  }
  // a copy or a wide value we know nothing about
  if (!instr->is(fir::IntrinsicSubType::Memset) ||
      instr->args[1].get_type()->get_size() != 1 ||
      !instr->args[1].is_constant()) {
    new_in_one.reset(false);
    new_in_zero.reset(false);
    return;
  }
  auto size = instr->args[2];
  auto cval = instr->args[1].as_constant()->as_int();
  if (!size.is_constant() || result != StackOffsetResult::KnownLocal) {
    // TODO: can improve this to only clear above the offset
    if (cval == 0 && result == StackOffsetResult::UnknownLocal) {
      new_in_one.reset(false);
    } else if (cval == 255 && result == StackOffsetResult::UnknownLocal) {
      new_in_zero.reset(false);
    } else {
      new_in_one.reset(false);
      new_in_zero.reset(false);
    }
    return;
  }
  auto csize = size.as_constant()->as_int();
  for (u64 i = 0; i < csize; i++) {
    auto curr_off = offset + i;
    new_in_one.set(curr_off * 8, 8, static_cast<u8>(cval));
    new_in_zero.set(curr_off * 8, 8, ~static_cast<u8>(cval));
  }
}

void StackKnownBits::update_call(fir::Instr instr, utils::BitSet<> &new_in_one,
                                 utils::BitSet<> &new_in_zero,
                                 StackKnowCache &cache) {
  for (auto arg : instr->args) {
    if (arg.get_type()->is_ptr()) {
      u64 offset = 0;
      auto result = get_stack_offset(offset, arg, cache);
      // fmt::println("Result for {} arg: knwn {} unknwn {}", arg,
      //              result == StackOffsetResult::KnownLocal,
      //              result == StackOffsetResult::UnknownLocal);
      cache.insert({arg, {.result = result, .offset = offset}});
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
                                 StackKnowCache &cache,
                                 TMap<fir::ValueR, i128> &known_load_values) {
  u64 offset = 0;
  auto result = get_stack_offset(offset, instr->args[0], cache);
  // fmt::println("Load {}", instr->args[0]);
  cache.insert({instr->args[0], {.result = result, .offset = offset}});
  if (result == StackOffsetResult::UnknownLocal) {
    return;
  }
  if (result != StackOffsetResult::KnownLocal) {
    return;
  }

  auto load_width = instr->result_type()->get_size() * 8;
  if (load_width > 64) {
    return;
    // TODO: impl;
  }
  if (offset * 8 + load_width >= new_in_one.bit_size()) {
    return;
  }
  u64 in_zero = new_in_zero.get(offset * 8, load_width);
  u64 in_one = new_in_one.get(offset * 8, load_width);

  ASSERT((in_one & in_zero) == 0);

  u64 known_bits = in_zero | in_one;
  u64 mask = load_width == 64 ? std::bit_cast<u64>(static_cast<i64>(-1))
                              : ((1ULL << load_width) - 1);
  if ((known_bits & mask) != mask) {
    // in case it is in here from previous iteration
    known_load_values.erase(instr.result());
    return;
  }
  known_load_values[instr.result()] = in_one;
}

bool StackKnownBits::update_store(fir::Instr instr, utils::BitSet<> &new_in_one,
                                  utils::BitSet<> &new_in_zero,
                                  StackKnowCache &cache) {
  u64 offset = 0;
  auto result = get_stack_offset(offset, instr->args[0], cache);
  // fmt::println("Store {}", instr->args[0]);
  cache.insert({instr->args[0], {.result = result, .offset = offset}});
  if (result == StackOffsetResult::KnownNonLocal) {
    return true;
  }

  // the known bits are tracked in 64 bit words, wider values count as unknown
  if (instr->args[1].is_constant() &&
      ((instr->args[1].as_constant()->is_int() &&
        instr->args[1].get_type()->get_bitwidth() <= 64) ||
       instr->args[1].as_constant()->is_float() ||
       instr->args[1].as_constant()->is_null())) {
    u64 value = 0;
    bool is_int = instr->args[1].as_constant()->is_int();
    bool is_float = instr->args[1].as_constant()->is_float();
    bool is_null = instr->args[1].as_constant()->is_null();
    if (is_int) {
      value = instr->args[1].as_constant()->as_int();
    } else if (is_null) {
      value = 0;
    } else if (is_float) {
      value = std::bit_cast<u64>(instr->args[1].as_constant()->as_float());
    } else {
      UNREACH();
    }
    if (result == StackOffsetResult::KnownLocal) {
      auto size = instr->result_type()->get_size() * 8;
      u64 mask = 0;
      if (size == 64) {
        mask = ~mask;
      } else {
        mask = ((1ULL << size) - 1);
      }
      // fmt::println("At offset {} size {} value {}", offset, size, value);
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

  // storing what was loaded from a constant global (an expanded memcpy)
  if (result == StackOffsetResult::KnownLocal) {
    const auto bytes = constant_load_bytes(instr->args[1]);
    if (bytes && (offset + bytes->size()) * 8 <= new_in_one.bit_size()) {
      for (u64 i = 0; i < bytes->size(); i++) {
        new_in_one.set((offset + i) * 8, 8, (*bytes)[i]);
        new_in_zero.set((offset + i) * 8, 8, static_cast<u8>(~(*bytes)[i]));
      }
      return true;
    }
  }

  if (instr->args[1].get_type()->is_ptr()) {
    u64 offset = 0;
    auto result = get_stack_offset(offset, instr->args[1], cache);
    // if we store a local pointer to somwhere we gotta cancel hard to track
    if (result == StackOffsetResult::KnownLocal ||
        result == StackOffsetResult::UnknownLocal) {
      // fmt::println("Failed store!! {} Known:{}", instr,
      //              result == StackOffsetResult::KnownLocal);
      return false;
    }
  }

  // we dont know what we write so just reset the bits accordingly
  if (result == StackOffsetResult::KnownLocal) {
    auto size = instr->result_type()->get_size() * 8;
    // TOOD: impl
    if (offset * 8 >= new_in_one.bit_size()) {
      return false;
    }
    if (size > 64) {
      new_in_one.reset(false);
      new_in_zero.reset(false);
    } else {
      new_in_one.set(offset * 8, size, 0);
      new_in_zero.set(offset * 8, size, 0);
    }
  }
  if (result == StackOffsetResult::UnknownLocal) {
    new_in_one.reset(false);
    new_in_zero.reset(false);
  }
  return true;
}

struct SROARes {
  fir::TypeR type;
  TVec<fir::Use> associated_values;
  u32 size;
};

// static bool handle_arg(fir::ValueR arg, fir::Use use, fir::TypeR type,
//                 StackKnowCache &cache, TOMap<u32, SROARes> &acceses) {
//   auto r = cache.at(arg);
//   if (r.result == StackOffsetResult::UnknownLocal) {
//     return false;
//   }
//   // fmt::println("{}", arg);
//   if (r.result == StackOffsetResult::KnownLocal) {
//     auto lower_bound = acceses.lower_bound(r.offset);
//     auto v_size = arg.get_type()->get_size();
//     if (lower_bound == acceses.end()) {
//       // its upper bound so just insert cant collide
//       acceses.insert(
//           {r.offset,
//            {.type = type, .associated_values = {use}, .size = v_size}});
//     } else if (lower_bound->first == r.offset) {
//       // perfect match so types neeed to match
//       if (!lower_bound->second.type.is_valid() ||
//           lower_bound->second.type != type) {
//         lower_bound->second.type = fir::TypeR{fir::TypeR::invalid()};
//         lower_bound->second.size = std::max(lower_bound->second.size,
//         v_size);
//       } else {
//         lower_bound->second.associated_values.push_back(use);
//       }
//     } else {
//       // we didnt match but we might overlap with the next need invalidate it
//       // then
//       auto curr = lower_bound;
//       bool overlapped = false;
//       while (curr != acceses.end() && curr->first < r.offset + v_size) {
//         lower_bound->second.type = fir::TypeR{fir::TypeR::invalid()};
//         overlapped = true;
//         curr = curr++;
//       }
//       auto typee = overlapped ? fir::TypeR{fir::TypeR::invalid()} : type;
//       acceses.insert(
//           {r.offset,
//            {.type = typee, .associated_values = {use}, .size = v_size}});
//     }
//   }
//   return true;
// }

StackOffsetResult get_stack_offset(u64 &offset, fir::ValueR ptr,
                                   StackKnowCache &cache) {
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
    if (arg_parent == entry_block) {
      return StackOffsetResult::KnownNonLocal;
    }

    // TODO: improve!
    return StackOffsetResult::UnknownLocal;
  }
  if (ptr.is_instr()) {
    auto ptr_instr = ptr.as_instr();
    if (ptr_instr->is(fir::InstrType::AllocaInstr)) {
      // allocas of the entry block are in the cache, any other one is not
      // tracked
      return StackOffsetResult::UnknownLocal;
    }
    if (ptr_instr->is(fir::InstrType::LoadInstr)) {
      // TODO what if what we load comes from args for example it cant contain a
      // local one
      return StackOffsetResult::UnknownLocal;
    }
    if (ptr_instr->is(fir::InstrType::CallInstr) ||
        ptr_instr->is(fir::IntrinsicSubType::InlineAsm) ||
        ptr_instr->is(fir::IntrinsicSubType::InlineAsmPure)) {
      for (auto arg : ptr_instr->args) {
        if (arg.get_type()->is_ptr()) {
          u64 sub_offset = 0;
          auto sub_result = get_stack_offset(sub_offset, arg, cache);
          if (!cache.contains(arg)) {
            cache.insert({arg, {.result = sub_result, .offset = sub_offset}});
          }
          if (sub_result == StackOffsetResult::UnknownLocal ||
              sub_result == StackOffsetResult::KnownLocal) {
            return StackOffsetResult::UnknownLocal;
          }
        }
      }
      return StackOffsetResult::KnownNonLocal;
    }
    if (ptr_instr->is(fir::InstrType::BinaryInstr)) {
      u64 sub_offset = 0;
      StackOffsetResult sub_result;
      u32 offset_index = 0;

      if (ptr_instr->args[0].get_type()->is_ptr()) {
        sub_offset = 0;
        sub_result = get_stack_offset(sub_offset, ptr_instr->args[0], cache);
        if (!cache.contains(ptr_instr->args[0])) {
          cache.insert({ptr_instr->args[0],
                        {.result = sub_result, .offset = sub_offset}});
        }
        offset_index = 1;
      } else {
        sub_offset = 0;
        sub_result = get_stack_offset(sub_offset, ptr_instr->args[1], cache);
        if (!cache.contains(ptr_instr->args[1])) {
          cache.insert({ptr_instr->args[1],
                        {.result = sub_result, .offset = sub_offset}});
        }
        offset_index = 0;
      }
      if (sub_result == StackOffsetResult::UnknownLocal) {
        return StackOffsetResult::UnknownLocal;
      }
      if (sub_result == StackOffsetResult::KnownLocal) {
        if (ptr_instr->args[offset_index].is_constant() &&
            ptr_instr->args[offset_index].as_constant()->is_int()) {
          auto value = ptr_instr->args[offset_index].as_constant()->as_int();
          auto ty = static_cast<fir::BinaryInstrSubType>(ptr_instr->subtype);
          if (ty == fir::BinaryInstrSubType::IntAdd ||
              ty == fir::BinaryInstrSubType::PtrAdd) {
            offset += sub_offset + value;
          } else {
            TODO("IMPL");
          }
          return StackOffsetResult::KnownLocal;
        }
        return StackOffsetResult::UnknownLocal;
      }
      if (sub_result == StackOffsetResult::KnownNonLocal &&
          ptr_instr->is(fir::BinaryInstrSubType::PtrAdd) &&
          offset_index == 1) {
        return StackOffsetResult::KnownNonLocal;
      }
      // just assume the worst
      return StackOffsetResult::UnknownLocal;
    }
    if (ptr_instr->is(fir::IntrinsicSubType::TlsAddress)) {
      return StackOffsetResult::KnownNonLocal;
    }
    // points into the frame, at none of its objects in particular
    if (ptr_instr->is(fir::IntrinsicSubType::FrameAddress)) {
      return StackOffsetResult::UnknownLocal;
    }
    if (ptr_instr->is(fir::VectorISubType::Broadcast)) {
      u64 sub_offset = 0;
      auto sub_result = get_stack_offset(sub_offset, ptr_instr->args[0], cache);
      offset += sub_offset;
      return sub_result;
    }
    if (ptr_instr->is(fir::InstrType::Conversion) &&
        static_cast<fir::ConversionSubType>(ptr_instr->subtype) ==
            fir::ConversionSubType::IntToPtr) {
      return StackOffsetResult::UnknownLocal;
    }
    if (ptr_instr->is(fir::InstrType::ExtractElement)) {
      return StackOffsetResult::UnknownLocal;
    }

    if (ptr_instr->is(fir::InstrType::SelectInstr)) {
      u64 sub0_offset = 0;
      auto sub0_result =
          get_stack_offset(sub0_offset, ptr_instr->args[1], cache);
      if (!cache.contains(ptr_instr->args[1])) {
        cache.insert({ptr_instr->args[1],
                      {.result = sub0_result, .offset = sub0_offset}});
      }
      u64 sub1_offset = 0;
      auto sub1_result =
          get_stack_offset(sub1_offset, ptr_instr->args[2], cache);
      if (!cache.contains(ptr_instr->args[2])) {
        cache.insert({ptr_instr->args[2],
                      {.result = sub1_result, .offset = sub1_offset}});
      }
      if (sub0_result == StackOffsetResult::KnownNonLocal &&
          sub1_result == StackOffsetResult::KnownNonLocal) {
        return StackOffsetResult::KnownNonLocal;
      }
      if (sub0_result == StackOffsetResult::KnownLocal &&
          sub1_result == StackOffsetResult::KnownLocal &&
          sub0_offset == sub1_offset) {
        offset += sub0_offset;
        return StackOffsetResult::KnownLocal;
      }
      return StackOffsetResult::UnknownLocal;
    }
    fmt::println("{}", ptr_instr);
  }
  fmt::println("{}", ptr);
  TODO("impl?");
  return StackOffsetResult::UnknownLocal;
}

// Whether a pointer can point into the stack of the function. A loaded pointer
// or a call result can only if a stack pointer was handed to a call before.
static bool may_point_to_stack(fir::ValueR ptr, u32 depth = 0) {
  if (ptr.is_constant()) {
    return false;
  }
  if (ptr.is_bb_arg()) {
    auto parent = ptr.as_bb_arg()->get_parent();
    return parent != parent->get_parent()->get_entry();
  }
  auto instr = ptr.as_instr();
  if (instr->is(fir::InstrType::LoadInstr) ||
      instr->is(fir::InstrType::CallInstr)) {
    return false;
  }
  if (instr->is(fir::InstrType::AllocaInstr) || depth > 8) {
    return true;
  }
  if (!instr->is(fir::InstrType::BinaryInstr) &&
      !instr->is(fir::InstrType::SelectInstr)) {
    return true;
  }
  for (auto arg : instr->args) {
    if (arg.get_type()->is_ptr() && may_point_to_stack(arg, depth + 1)) {
      return true;
    }
  }
  return false;
}

// no call or asm gets a pointer into the stack (except the ones that only read)
static bool stack_pointers_stay_local(fir::Function &func) {
  for (auto bb : func.basic_blocks) {
    for (auto instr : bb->instructions) {
      const bool is_call = instr->is(fir::InstrType::CallInstr);
      if (!is_call && !instr->is(fir::IntrinsicSubType::InlineAsm) &&
          !instr->is(fir::IntrinsicSubType::InlineAsmPure)) {
        continue;
      }
      if (is_string_call(instr)) {
        continue;
      }
      for (auto arg : instr->args) {
        if (arg.get_type()->is_ptr() && may_point_to_stack(arg)) {
          return false;
        }
      }
    }
  }
  return true;
}

PreservedAnalysis StackKnownBits::apply(fir::Context &ctx,
                                        fir::Function &func) {
  ZoneScopedNC("StackKnownBits", COLOR_OPTIMF);

  u64 stack_size = 0;
  StackKnowCache cache;

  for (auto instr : func.basic_blocks[0]->instructions) {
    if (instr->is(fir::InstrType::AllocaInstr)) {
      auto a1 = instr->get_arg(0);
      if (!a1.is_constant()) {
        failure({.reason = "Failed cause of dynamic alloca", .loc = instr});
        return PreservedAnalysis::all();
      }
      cache.insert({instr.result(),
                    {.result = StackOffsetResult::KnownLocal,
                     .offset = stack_size / 8}});
      stack_size += a1.as_constant()->as_int() * 8;
    }
  }

  if (stack_size != 0 && stack_pointers_stay_local(func)) {
    for (auto bb : func.basic_blocks) {
      for (auto instr : bb->instructions) {
        if (instr->is(fir::InstrType::LoadInstr) &&
            instr.result_type()->is_ptr()) {
          cache.insert(
              {instr.result(),
               {.result = StackOffsetResult::KnownNonLocal, .offset = 0}});
        }
      }
    }
  }

  if (stack_size == 0) {
    return PreservedAnalysis::all();
  }
  if (stack_size > 4096) {
    failure({.reason = "Failed cause too much stack space",
             .loc = func.get_entry()});
    return PreservedAnalysis::all();
  }
  // fmt::println("Got {} bits\n", stack_size);

  TVec<utils::BitSet<>> exit_known_one;
  TVec<utils::BitSet<>> exit_known_zero;

  exit_known_one.resize(func.n_bbs(), utils::BitSet<>::empty(stack_size));
  exit_known_zero.resize(func.n_bbs(), utils::BitSet<>::empty(stack_size));

  std::deque<u32, utils::TempAlloc<u32>> worklist;
  for (u32 i = 0; i < func.n_bbs(); i++) {
    worklist.push_back(i);
  }
  CFG &cfg = AnalysisManager::cfg(func);

  auto new_in_one = utils::BitSet<>::empty(stack_size);
  auto new_in_zero = utils::BitSet<>::empty(stack_size);
  // auto test = utils::BitSet<>::empty(stack_size);

  TVec<bool> visited(func.n_bbs(), false);
  TMap<fir::ValueR, i128> known_load_values;
  TMap<fir::Instr, i128> known_call_values;
  TVec<fir::Instr> load_stores;

  while (!worklist.empty()) {
    auto curr = worklist.front();
    worklist.pop_front();

    if (cfg.bbrs[curr].pred.empty()) {
      // maybe could just decide on a static value
      // sicne is poison?
      new_in_one.reset(false);
      new_in_zero.reset(false);
    } else {
      new_in_one.reset(true);
      new_in_zero.reset(true);
      bool any_visited = false;
      for (auto p : cfg.bbrs[curr].pred) {
        // a predecessor that was not processed yet knows everything, so a
        // loop header keeps what the preheader knows until the latch says
        // otherwise
        if (!visited[p]) {
          continue;
        }
        any_visited = true;
        new_in_one.mul(exit_known_one[p]);
        new_in_zero.mul(exit_known_zero[p]);
      }
      if (!any_visited) {
        // nothing is known about the block yet, it is pushed again once a
        // predecessor has been processed
        continue;
      }
    }
    // test.assign(new_in_one);
    // test.mul(new_in_zero);
    // ASSERT(!test.any());

    for (auto instr : cfg.bbrs[curr].bb->instructions) {
      if (instr->is(fir::InstrType::Conversion) &&
          static_cast<fir::ConversionSubType>(instr->subtype) ==
              fir::ConversionSubType::PtrToInt) {
        // TODO: this can be improved depending on the usage
        // but important to not have escaping pointers
        failure({.reason = "PtrToInt escape", .loc = instr});
        return PreservedAnalysis::all();
      }
      // asm may write through its pointers like a call
      if (is_string_call(instr)) {
        update_string_call(instr, new_in_one, new_in_zero, cache,
                           known_call_values);
      } else if (instr->is(fir::InstrType::CallInstr) ||
                 instr->is(fir::IntrinsicSubType::InlineAsm) ||
                 instr->is(fir::IntrinsicSubType::InlineAsmPure)) {
        update_call(instr, new_in_one, new_in_zero, cache);
        load_stores.push_back(instr);
        // fmt::println("CALL\n{}\n{}", new_in_zero, new_in_one);
      } else if (instr->is(fir::IntrinsicSubType::Memset) ||
                 instr->is(fir::IntrinsicSubType::Memcpy) ||
                 instr->is(fir::IntrinsicSubType::Memmove)) {
        update_mem_intrinsic(instr, new_in_one, new_in_zero, cache);
      } else if (instr->is(fir::InstrType::StoreInstr)) {
        // important to not have escaping pointers
        if (!update_store(instr, new_in_one, new_in_zero, cache)) {
          failure({.reason = "Storing a local pointer away", .loc = instr});
          return PreservedAnalysis::all();
        }
        load_stores.push_back(instr);
        // fmt::println("STORE\n{}\n{}", new_in_zero, new_in_one);
      } else if (instr->is(fir::InstrType::LoadInstr)) {
        update_load(instr, new_in_one, new_in_zero, cache, known_load_values);
        load_stores.push_back(instr);
      }
    }

    bool changed = !visited[curr];
    visited[curr] = true;
    if (new_in_one != exit_known_one[curr]) {
      exit_known_one[curr].assign(new_in_one);
      changed = true;
    }
    if (new_in_zero != exit_known_zero[curr]) {
      exit_known_zero[curr].assign(new_in_zero);
      changed = true;
    }
    if (changed) {
      for (auto succ : cfg.bbrs[curr].succ) {
        worklist.push_back(succ);
      }
    }
  }

  // (void)ctx;
  bool modified = false;
  for (auto &[l, v] : known_load_values) {
    auto load = l;
    modified |= load.get_n_uses() > 0;

    if (l.get_type()->is_float()) {
      auto widht = l.get_type()->as_float();
      if (widht == 32) {
        auto val = ctx->get_constant_value(
            std::bit_cast<f32>(static_cast<u32>(v)), l.get_type());
        load.replace_all_uses(fir::ValueR(val));
        cache.erase(load);
      } else if (widht == 64) {
        // TODO: this might lead to issues with teh cast?
        auto val = ctx->get_constant_value(
            std::bit_cast<f64>(static_cast<u64>(v)), l.get_type());
        load.replace_all_uses(fir::ValueR(val));
        cache.erase(load);
      }
    } else if (l.get_type()->is_int()) {
      auto val = ctx->get_constant_value(v, l.get_type());
      load.replace_all_uses(fir::ValueR(val));
      cache.erase(load);
    }
  }
  TVec<fir::Instr> folded_calls;
  for (auto &[call, v] : known_call_values) {
    folded_calls.push_back(call);
  }
  for (auto call : folded_calls) {
    call->replace_all_uses(fir::ValueR(
        ctx->get_constant_value(known_call_values[call], call.result_type())));
    call.destroy();
    modified = true;
  }
  // only uses of loads get replaced
  return modified ? PreservedAnalysis::cfg_only() : PreservedAnalysis::all();
}

}  // namespace foptim::optim
