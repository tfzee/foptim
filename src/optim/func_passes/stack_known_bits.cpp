#include "stack_known_bits.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/cfg.hpp"
#include "utils/arena.hpp"
#include <deque>
#include <llvm/ADT/STLExtras.h>
#include <map>
#include <unistd.h>

namespace foptim::optim {

void StackKnownBits::update_call(fir::Instr instr, utils::BitSet<> &new_in_one,
                                 utils::BitSet<> &new_in_zero,
                                 StackKnowCache &cache) {

  if (instr->args[0].is_constant() && instr->args[0].as_constant()->is_func() &&
      (instr->args[0].as_constant()->as_func()->name == "foptim.memset")) {
    auto ptr = instr->args[1];
    auto val = instr->args[2];
    auto size = instr->args[3];
    u64 offset = 0;
    auto result = get_stack_offset(offset, ptr, cache);
    cache.insert({ptr, {result, offset}});
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
      cache.insert({arg, {result, offset}});
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
  cache.insert({instr->args[0], {result, offset}});
  if (result == StackOffsetResult::UnknownLocal) {
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
  if ((known_bits & mask) != mask) {
    // in case it is in here from previous iteration
    known_load_values.erase(fir::ValueR(instr));
    return;
  }
  known_load_values[fir::ValueR(instr)] = in_one;
}

bool StackKnownBits::update_store(fir::Instr instr, utils::BitSet<> &new_in_one,
                                  utils::BitSet<> &new_in_zero,
                                  StackKnowCache &cache) {
  u64 offset = 0;
  auto result = get_stack_offset(offset, instr->args[0], cache);
  // fmt::println("Store {}", instr->args[0]);
  cache.insert({instr->args[0], {result, offset}});
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

struct SROARes {
  fir::TypeR type;
  TVec<fir::Use> associated_values;
  u32 size;
};

bool handle_arg(fir::ValueR arg, fir::Use use, fir::TypeR type,
                StackKnowCache &cache, TOMap<u32, SROARes> &acceses) {
  auto r = cache.at(arg);
  if (r.result == StackOffsetResult::UnknownLocal) {
    return false;
  }
  fmt::println("{}", arg);
  if (r.result == StackOffsetResult::KnownLocal) {
    auto lower_bound = acceses.lower_bound(r.offset);
    auto v_size = arg.get_type()->get_size();
    if (lower_bound == acceses.end()) {
      // its upper bound so just insert cant collide
      acceses.insert({r.offset, {type, {use}, v_size}});
    } else if (lower_bound->first == r.offset) {
      // perfect match so types neeed to match
      if (!lower_bound->second.type.is_valid() ||
          lower_bound->second.type != type) {
        lower_bound->second.type = fir::TypeR{fir::TypeR::invalid()};
        lower_bound->second.size = std::max(lower_bound->second.size, v_size);
      } else {
        lower_bound->second.associated_values.push_back(use);
      }
    } else {
      // we didnt match but we might overlap with the next need invalidate it
      // then
      auto curr = lower_bound;
      bool overlapped = false;
      while (curr != acceses.end() && curr->first < r.offset + v_size) {
        lower_bound->second.type = fir::TypeR{fir::TypeR::invalid()};
        overlapped = true;
        curr = curr++;
      }
      auto typee = overlapped ? fir::TypeR{fir::TypeR::invalid()} : type;
      acceses.insert({r.offset, {typee, {use}, v_size}});
    }
  }
  return true;
}

void StackKnownBits::execute_sroa(TVec<fir::Instr> &load_stores,
                                  StackKnowCache &cache,
                                  fir::BasicBlock entry_bb) {
  // ensuring we know all accesses
  TOMap<u32, SROARes> acceses;
  // find all load/stores
  // fmt::println("Number {}", load_stores.size());
  for (auto &loadstore_instr : load_stores) {
    if (loadstore_instr->is(fir::InstrType::LoadInstr) ||
        loadstore_instr->is(fir::InstrType::StoreInstr)) {
      auto instr_arg = loadstore_instr->args[0];
      if (!handle_arg(instr_arg, fir::Use::norm(loadstore_instr, 0),
                      loadstore_instr->get_type(), cache, acceses)) {
        return;
      }
    } else if (loadstore_instr->is(fir::InstrType::CallInstr)) {
      // TODO: improve
      // for now it just escapes abort
      return;
    } else {
      fmt::println("{}", loadstore_instr);
      TODO("IMPL");
    }
  }

  for (auto &[off, res] : acceses) {
    // now insert new alloca for each var
    // and then replace all uses to this new alloca
    // fmt::println("{} N:{} T:{}@{}", off, res.associated_values.size(),
    // res.type,
    //              res.size);
    if (!res.type.is_valid()) {
      continue;
    }
    auto *ctx = entry_bb->get_parent().func->ctx;
    auto bb = fir::Builder(entry_bb);
    auto alloca = bb.build_alloca(
        fir::ValueR(ctx->get_constant_value(res.size, ctx->get_int_type(32))));
    alloca.as_instr()->add_attrib("alloca::type", res.type);
    for (auto &v : res.associated_values) {
      v.replace_use(alloca);
    }
  }
}

StackOffsetResult get_stack_offset(u64 &offset, fir::ValueR ptr,
                                   StackKnowCache &cache) {
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
    if (arg_parent == entry_block) {
      return StackOffsetResult::KnownNonLocal;
    }

    // TODO: improve!
    return StackOffsetResult::UnknownLocal;
  }
  if (ptr.is_instr()) {
    auto ptr_instr = ptr.as_instr();
    if (ptr_instr->is(fir::InstrType::AllocaInstr)) {
      fmt::println("Cache should have handled this\n");
      UNREACH();
    }
    if (ptr_instr->is(fir::InstrType::LoadInstr)) {
      // TODO what if what we load comes from args for example it cant contain a
      // local one
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
          cache.insert({ptr_instr->args[0], {sub_result, sub_offset}});
        }
        offset_index = 1;
      } else {
        sub_offset = 0;
        sub_result = get_stack_offset(sub_offset, ptr_instr->args[1], cache);
        if (!cache.contains(ptr_instr->args[1])) {
          cache.insert({ptr_instr->args[1], {sub_result, sub_offset}});
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
          if ((BinaryInstrSubType)ptr_instr->subtype ==
              BinaryInstrSubType::IntAdd) {
            offset += sub_offset + value;
          } else {
            TODO("IMPL");
          }
          return StackOffsetResult::KnownLocal;
        }
        return StackOffsetResult::UnknownLocal;
      }
      // just assume the worst
      return StackOffsetResult::UnknownLocal;
    }
    if (ptr_instr->is(fir::InstrType::Conversion) &&
        (ConversionSubType)ptr_instr->subtype == ConversionSubType::IntToPtr) {
      return StackOffsetResult::UnknownLocal;
    }
    if (ptr_instr->is(fir::InstrType::SelectInstr)) {
      u64 sub0_offset = 0;
      auto sub0_result =
          get_stack_offset(sub0_offset, ptr_instr->args[1], cache);
      if (!cache.contains(ptr_instr->args[1])) {
        cache.insert({ptr_instr->args[1], {sub0_result, sub0_offset}});
      }
      u64 sub1_offset = 0;
      auto sub1_result =
          get_stack_offset(sub1_offset, ptr_instr->args[2], cache);
      if (!cache.contains(ptr_instr->args[2])) {
        cache.insert({ptr_instr->args[2], {sub1_result, sub1_offset}});
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

void StackKnownBits::apply(fir::Context &ctx, fir::Function &func) {
  using namespace foptim::fir;
  ZoneScopedN("StackKnownBits");

  u64 stack_size = 0;
  StackKnowCache cache;

  for (auto instr : func.basic_blocks[0]->instructions) {
    if (instr->is(fir::InstrType::AllocaInstr)) {
      auto a1 = instr->get_arg(0);
      if (!a1.is_constant()) {
        failure({"Failed cause of dynamic alloca", instr});
        return;
      }
      cache.insert(
          {fir::ValueR{instr}, {StackOffsetResult::KnownLocal, stack_size}});
      stack_size += a1.as_constant()->as_int() * 8;
    }
  }

  fmt::println("Got {} bits\n", stack_size);
  if (stack_size == 0 || stack_size > 4096) {
    failure(
        {"Failed cause either none or too much stack space", func.get_entry()});
    return;
  }

  TVec<utils::BitSet<>> exit_known_one;
  TVec<utils::BitSet<>> exit_known_zero;

  exit_known_one.resize(func.n_bbs(), utils::BitSet<>::empty(stack_size));
  exit_known_zero.resize(func.n_bbs(), utils::BitSet<>::empty(stack_size));

  std::deque<u32, utils::TempAlloc<u32>> worklist;
  for (u32 i = 0; i < func.n_bbs(); i++) {
    worklist.push_back(i);
  }
  CFG cfg{func};

  auto new_in_one = utils::BitSet<>::empty(stack_size);
  auto new_in_zero = utils::BitSet<>::empty(stack_size);

  TMap<ValueR, i128> known_load_values;
  TVec<fir::Instr> load_stores;

  while (!worklist.empty()) {
    auto curr = worklist.front();
    worklist.pop_front();

    new_in_one.reset(true);
    new_in_zero.reset(true);
    for (auto p : cfg.bbrs[curr].pred) {
      new_in_one.mul(exit_known_one[p]);
      new_in_zero.mul(exit_known_zero[p]);
    }

    for (auto instr : cfg.bbrs[curr].bb->instructions) {

      if (instr->is(fir::InstrType::Conversion) &&
          (ConversionSubType)instr->subtype == ConversionSubType::PtrToInt) {
        // TODO: this can be improved depending on the usage
        // but important to not have escaping pointers
        failure({"PtrToInt escape", instr});
        return;
      }
      if (instr->is(fir::InstrType::CallInstr)) {
        update_call(instr, new_in_one, new_in_zero, cache);
        load_stores.push_back(instr);
        // fmt::println("CALL\n{}\n{}", new_in_zero, new_in_one);
      } else if (instr->is(fir::InstrType::StoreInstr)) {
        // important to not have escaping pointers
        if (!update_store(instr, new_in_one, new_in_zero, cache)) {
          failure({"Storing a local pointer away", instr});
          return;
        }
        load_stores.push_back(instr);
        // fmt::println("STORE\n{}\n{}", new_in_zero, new_in_one);
      } else if (instr->is(fir::InstrType::LoadInstr)) {
        update_load(instr, new_in_one, new_in_zero, cache, known_load_values);
        load_stores.push_back(instr);
        // fmt::println("LOAD\n{}\n{}", new_in_zero, new_in_one);
      }
    }

    bool changed = false;
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
  for (auto &[l, v] : known_load_values) {
    auto load = l;

    if (l.get_type()->is_float()) {
      auto widht = l.get_type()->as_float();
      if (widht == 32) {
        auto val =
            ctx->get_constant_value(std::bit_cast<f32>((u32)v), l.get_type());
        load.replace_all_uses(fir::ValueR(val));
        cache.erase(load);
      } else if (widht == 64) {
        //TODO: this might lead to issues with teh cast?
        auto val =
            ctx->get_constant_value(std::bit_cast<f64>((u64)v), l.get_type());
        load.replace_all_uses(fir::ValueR(val));
        cache.erase(load);
      }
    } else {
      auto val = ctx->get_constant_value(v, l.get_type());
      load.replace_all_uses(fir::ValueR(val));
      cache.erase(load);
    }
  }

  // SROA
  // at this point we know local pointers dont escape
  // other then potentially through func calls
  execute_sroa(load_stores, cache, func.get_entry());
}

} // namespace foptim::optim
