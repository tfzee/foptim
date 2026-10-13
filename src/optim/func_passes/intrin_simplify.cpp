#include "intrin_simplify.hpp"

#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/access_analysis.hpp"
#include "optim/analysis/basic_alias_test.hpp"
#include "optim/helper/helper.hpp"

namespace foptim::optim {

// copies and fills up to this many bytes become plain moves
constexpr i128 MaxInlineCopy = 64;
// memcmp compared for (in)equality only up to this many bytes become loads
constexpr i128 MaxMemcmpEq = 32;

static fir::ValueR offset_ptr(fir::Context &ctx, fir::Builder &b,
                              fir::ValueR base, i128 off) {
  if (off == 0) {
    return base;
  }
  return b.build_binary_op(base, fir::ValueR{ctx->get_constant_int(off, 64)},
                           fir::BinaryInstrSubType::PtrAdd);
}

/*If we memcpy into a local alloca and the len of the memcpy is equal to alloca
length and afterwards we only read from it*/
static bool delete_useless_memcpy(fir::Instr instr) {
  auto target_ptr = instr->args[0];
  auto copy_len = instr->args[2];
  if (!target_ptr.is_instr() || !copy_len.is_constant()) {
    return false;
  }
  auto target_ptr_instr = target_ptr.as_instr();
  if (!target_ptr_instr->is(fir::InstrType::AllocaInstr)) {
    return false;
  }
  auto alloca_len = target_ptr_instr->args[0];
  if (!alloca_len.is_constant()) {
    return false;
  }
  if (alloca_len.as_constant()->as_int() !=
      copy_len.as_constant()->as_int() *
          static_cast<i128>(instr->memcpy_elem_size())) {
    return false;
  }
  for (auto u : target_ptr_instr->get_uses()) {
    if (u.user == instr && u.argId == 0) {
      continue;
    }
    AccessResult res;
    useptr_access_analysis(u, res);
    if (res.IsWriten || res.VolatileRead || res.VolatileWrite || res.Escapes) {
      return false;
    }
  }
  target_ptr_instr->replace_all_uses(instr->args[1]);
  instr.destroy();
  return true;
}

static bool simplify_memcpy(fir::Instr instr, fir::BasicBlock bb,
                            fir::Context &ctx) {
  (void)bb;

  if (!instr->args[2].is_constant()) {
    return false;
  }
  // the count is in elements of the extra type
  auto size = instr->args[2].as_constant()->as_int() *
              static_cast<i128>(instr->memcpy_elem_size());

  // delete uselless memcpy
  if (delete_useless_memcpy(instr)) {
    return true;
  }

  if (size == 0) {
    instr.destroy();
    return true;
  }
  if (size > MaxInlineCopy) {
    return false;
  }
  auto dst_ptr = instr->args[0];
  auto src_ptr = instr->args[1];

  auto input_ty = guessType(src_ptr);
  auto output_ty = guessType(dst_ptr);
  const bool typed_size = size <= 8 || size == 16 || size == 32;
  const bool types_match =
      input_ty.typeless || output_ty.typeless ||
      (input_ty.type.is_valid() && output_ty.type.is_valid() &&
       input_ty.type->eql(*output_ty.type.get_raw_ptr()));
  // sizes without a matching type become a run of integer moves
  if (!typed_size || !types_match) {
    fir::Builder b{instr};
    i128 off = 0;
    while (off < size) {
      i128 len = 8;
      while (len > size - off) {
        len /= 2;
      }
      auto v = b.build_load(ctx->get_int_type(len * 8),
                            offset_ptr(ctx, b, src_ptr, off), false,
                            instr->Volatile);
      b.build_store(offset_ptr(ctx, b, dst_ptr, off), v, false,
                    instr->Volatile);
      off += len;
    }
    instr.destroy();
    return true;
  }
  if ((input_ty.typeless || output_ty.typeless) ||
      (input_ty.type.is_valid() && output_ty.type.is_valid() &&
       input_ty.type->eql(*output_ty.type.get_raw_ptr()))) {
    fir::Builder b{instr};

    fir::TypeR selected_type;
    if (size == 32) {
      selected_type = ctx->get_vec_type(ctx->get_int_type(64), 4);
    } else if (size == 16) {
      selected_type = ctx->get_vec_type(ctx->get_int_type(64), 2);
    } else {
      selected_type =
          input_ty.type.is_valid()
              ? input_ty.type
              : (output_ty.type.is_valid() ? output_ty.type
                                           : ctx->get_int_type(size * 8));
    }
    auto input = b.build_load(selected_type, src_ptr, false, instr->Volatile);
    b.build_store(dst_ptr, input, false, instr->Volatile);
    instr.destroy();
    return true;
  }
  return false;
}

static bool simplify_memset(fir::Instr instr, fir::Context &ctx) {
  auto dst_ptr = instr->args[0];
  auto value = instr->args[1];
  auto val_ty = value.get_type();
  if (!instr->args[2].is_constant_int() || val_ty->is_vec() ||
      val_ty->is_struct()) {
    return false;
  }
  const i128 count = instr->args[2].as_constant()->as_int();
  const i128 elem = val_ty->get_size();
  if (count == 0) {
    instr.destroy();
    return true;
  }
  if (count * elem > MaxInlineCopy) {
    return false;
  }
  fir::Builder b{instr};
  if (elem != 1) {
    // a wide value is stored once per element
    for (i128 i = 0; i < count; i++) {
      b.build_store(offset_ptr(ctx, b, dst_ptr, i * elem), value, false,
                    instr->Volatile);
    }
    instr.destroy();
    return true;
  }
  i128 off = 0;
  while (off < count) {
    i128 len = 8;
    while (len > count - off) {
      len /= 2;
    }
    fir::ValueR chunk = value;
    if (len != 1) {
      if (value.is_constant_int()) {
        u64 pattern = 0;
        const u64 byte = static_cast<u8>(value.as_constant()->as_int());
        for (i128 i = 0; i < len; i++) {
          pattern = (pattern << 8) | byte;
        }
        chunk = fir::ValueR{
            ctx->get_constant_value(pattern, ctx->get_int_type(len * 8))};
      } else {
        u64 splat = 0;
        for (i128 i = 0; i < len; i++) {
          splat = (splat << 8) | 1;
        }
        auto wide = b.build_zext(value, ctx->get_int_type(len * 8));
        chunk = b.build_int_mul(
            wide,
            fir::ValueR{
                ctx->get_constant_value(splat, ctx->get_int_type(len * 8))});
      }
    }
    b.build_store(offset_ptr(ctx, b, dst_ptr, off), chunk, false,
                  instr->Volatile);
    off += len;
  }
  instr.destroy();
  return true;
}

// A memmove that provably does not overlap is a memcpy, small ones read
// everything before they write anything.
static bool simplify_memmove(fir::Instr instr, fir::Context &ctx,
                             AliasAnalyis &aa) {
  if (instr->Volatile) {
    return false;
  }
  auto dst_ptr = instr->args[0];
  auto src_ptr = instr->args[1];
  const bool const_count = instr->args[2].is_constant_int();
  const i128 size =
      const_count ? instr->args[2].as_constant()->as_int() *
                        static_cast<i128>(instr->memcpy_elem_size())
                  : 0;
  if ((const_count && size == 0) || dst_ptr == src_ptr) {
    instr.destroy();
    return true;
  }
  const auto aa_size = const_count ? static_cast<u64>(size) : 0;
  if (aa.alias(dst_ptr, src_ptr, aa_size, aa_size) ==
      AliasAnalyis::AAResult::NoAlias) {
    instr->subtype = static_cast<u32>(fir::IntrinsicSubType::Memcpy);
    return true;
  }
  if (!const_count || size > MaxInlineCopy) {
    return false;
  }
  struct Chunk {
    i128 off;
    fir::TypeR ty;
    fir::ValueR val;
  };
  fir::Builder b{instr};
  TVec<Chunk> chunks;
  i128 off = 0;
  while (off < size) {
    i128 len = 32;
    while (len > size - off) {
      len /= 2;
    }
    fir::TypeR ty = len == 32   ? ctx->get_vec_type(ctx->get_int_type(64), 4)
                    : len == 16 ? ctx->get_vec_type(ctx->get_int_type(64), 2)
                                : ctx->get_int_type(len * 8);
    chunks.push_back({off, ty, b.build_load(ty, offset_ptr(ctx, b, src_ptr, off),
                                            false, false)});
    off += len;
  }
  for (auto &c : chunks) {
    b.build_store(offset_ptr(ctx, b, dst_ptr, c.off), c.val, false, false);
  }
  instr.destroy();
  return true;
}

// Compares of the result against zero only need to know if the bytes differ
// and the sign of one byte compare is a subtraction.
static bool simplify_memcmp(fir::Instr instr, fir::Context &ctx) {
  auto a = instr->args[0];
  auto b = instr->args[1];
  auto count = instr->args[2];
  auto i32_ty = ctx->get_int_type(32);
  auto zero = fir::ValueR{ctx->get_constant_int(0, 32)};
  if ((count.is_constant_int() && count.as_constant()->as_int() == 0) ||
      a == b) {
    instr->replace_all_uses(zero);
    instr.destroy();
    return true;
  }
  if (!count.is_constant_int()) {
    return false;
  }
  const i128 size = count.as_constant()->as_int();
  fir::Builder bld{instr};
  if (size == 1) {
    auto la = bld.build_zext(bld.build_load(ctx->get_int_type(8), a, false, false), i32_ty);
    auto lb = bld.build_zext(bld.build_load(ctx->get_int_type(8), b, false, false), i32_ty);
    instr->replace_all_uses(bld.build_int_sub(la, lb));
    instr.destroy();
    return true;
  }
  if (size > MaxMemcmpEq) {
    return false;
  }
  for (auto u : instr->uses) {
    const bool eq_zero_cmp =
        u.user->is(fir::InstrType::ICmp) && u.argId == 0 &&
        (u.user->is(fir::ICmpInstrSubType::EQ) ||
         u.user->is(fir::ICmpInstrSubType::NE)) &&
        u.user->args[1].is_constant_int() &&
        u.user->args[1].as_constant()->as_int() == 0;
    if (!eq_zero_cmp) {
      return false;
    }
  }
  // chunks may overlap, this only reads
  std::optional<fir::ValueR> acc;
  auto add_chunk = [&](i128 off, i128 len) {
    auto ty = ctx->get_int_type(len * 8);
    auto la = bld.build_load(ty, offset_ptr(ctx, bld, a, off), false, false);
    auto lb = bld.build_load(ty, offset_ptr(ctx, bld, b, off), false, false);
    fir::ValueR diff = bld.build_binary_op(la, lb, fir::BinaryInstrSubType::Xor);
    acc = acc ? bld.build_binary_op(*acc, diff, fir::BinaryInstrSubType::Or)
              : diff;
  };
  if (size >= 8) {
    for (i128 off = 0; off + 8 <= size; off += 8) {
      add_chunk(off, 8);
    }
    if (size % 8 != 0) {
      add_chunk(size - 8, 8);
    }
  } else {
    i128 len = 1;
    while (len * 2 <= size) {
      len *= 2;
    }
    add_chunk(0, len);
    if (len < size) {
      add_chunk(size - len, len);
    }
  }
  // all chunks have the same width
  auto differs = bld.build_int_cmp(
      *acc, fir::ValueR{ctx->get_constant_int(0, acc->get_type()->get_bitwidth())},
      fir::ICmpInstrSubType::NE);
  instr->replace_all_uses(bld.build_zext(differs, i32_ty));
  instr.destroy();
  return true;
}

static bool simplify(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx,
                     AliasAnalyis &aa) {
  if (instr->is(fir::IntrinsicSubType::Memcpy)) {
    return simplify_memcpy(instr, bb, ctx);
  }
  if (instr->is(fir::IntrinsicSubType::Memset)) {
    return simplify_memset(instr, ctx);
  }
  if (instr->is(fir::IntrinsicSubType::Memmove)) {
    return simplify_memmove(instr, ctx, aa);
  }
  if (instr->is(fir::IntrinsicSubType::Memcmp)) {
    return simplify_memcmp(instr, ctx);
  }
  return false;
}

PreservedAnalysis IntrinSimplify::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedNC("IntrinSimplify", COLOR_OPTIMF);

  bool modified = false;
  AliasAnalyis aa;
  for (foptim::fir::BasicBlock bb : func.basic_blocks) {
    auto &instrs = bb->get_instrs();
    for (size_t i = 0; i < instrs.size(); i++) {
      auto instr = instrs[i];
      modified |= simplify(instr, bb, ctx, aa);
    }
  }
  return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
}

} // namespace foptim::optim
