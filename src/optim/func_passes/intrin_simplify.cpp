#include "intrin_simplify.hpp"

#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/access_analysis.hpp"
#include "optim/helper/helper.hpp"

namespace foptim::optim {

// copies and fills up to this many bytes become plain moves
constexpr i128 MaxInlineCopy = 64;

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

static bool simplify(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx) {
  if (instr->is(fir::IntrinsicSubType::Memcpy)) {
    return simplify_memcpy(instr, bb, ctx);
  }
  if (instr->is(fir::IntrinsicSubType::Memset)) {
    return simplify_memset(instr, ctx);
  }
  return false;
}

PreservedAnalysis IntrinSimplify::apply(fir::Context &ctx, fir::Function &func) {
  ZoneScopedNC("IntrinSimplify", COLOR_OPTIMF);

  bool modified = false;
  for (foptim::fir::BasicBlock bb : func.basic_blocks) {
    auto &instrs = bb->get_instrs();
    for (size_t i = 0; i < instrs.size(); i++) {
      auto instr = instrs[i];
      modified |= simplify(instr, bb, ctx);
    }
  }
  return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
}

} // namespace foptim::optim
