#include "intrin_simplify.hpp"

#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/analysis/mem/access_analysis.hpp"
#include "optim/analysis/mem/basic_alias_test.hpp"
#include "optim/analysis/mem/mem_access.hpp"
#include "optim/analysis/mem/memory_ssa.hpp"
#include "optim/helper/helper.hpp"

namespace foptim::optim {

// copies and fills up to this many chunks become plain moves
constexpr size_t MaxInlineChunks = 8;
// a memmove holds all its chunks in registers at once
constexpr size_t MaxMoveChunks = 4;
// memcmp compared for (in)equality only up to this many bytes become loads
constexpr i128 MaxMemcmpEq = 32;
// memcmp with a sign result up to this many chunks
constexpr size_t MaxMemcmpChunks = 4;

static fir::ValueR offset_ptr(fir::Context &ctx, fir::Builder &b,
                              fir::ValueR base, i128 off) {
  if (off == 0) {
    return base;
  }
  return b.build_binary_op(base, fir::ValueR{ctx->get_constant_int(off, 64)},
                           fir::BinaryInstrSubType::PtrAdd);
}

struct Chunk {
  i128 off;
  i128 len;
};

static i128 pow2_floor(i128 v) {
  i128 p = 1;
  while (p * 2 <= v) {
    p *= 2;
  }
  return p;
}

// Splits size bytes into power of two chunks of at most max_len bytes. With
// overlap the tail is covered by chunks that reach back into bytes already
// done, which is only fine if repeating a chunk is harmless.
static TVec<Chunk> plan_chunks(i128 size, i128 max_len, bool overlap) {
  TVec<Chunk> res;
  i128 off = 0;
  while (size - off > max_len) {
    res.push_back({off, max_len});
    off += max_len;
    if (res.size() > MaxInlineChunks) {
      return res;
    }
  }
  const i128 rem = size - off;
  if (rem == 0) {
    return res;
  }
  const i128 q = pow2_floor(rem);
  TVec<Chunk> exact;
  i128 exact_off = off;
  for (i128 len = q; exact_off < size;) {
    while (len > size - exact_off) {
      len /= 2;
    }
    exact.push_back({exact_off, len});
    exact_off += len;
  }
  const bool one_wide = q * 2 <= size && q * 2 <= max_len;
  const size_t overlap_count = one_wide ? 1 : 2;
  if (!overlap || q == rem || exact.size() <= overlap_count) {
    res.insert(res.end(), exact.begin(), exact.end());
  } else if (one_wide) {
    res.push_back({size - q * 2, q * 2});
  } else {
    res.push_back({off, q});
    res.push_back({size - q, q});
  }
  return res;
}

static i128 max_vec_bytes(fir::Context &ctx) {
  return ctx.config->target.features.avx512f ? 64 : 32;
}

static fir::TypeR chunk_type(fir::Context &ctx, i128 len) {
  if (len >= 16) {
    return ctx->get_vec_type(ctx->get_int_type(64), len / 8);
  }
  return ctx->get_int_type(len * 8);
}

struct PtrOffset {
  fir::ValueR base;
  i128 off;
};

static PtrOffset split_ptr(fir::ValueR ptr) {
  const auto shape = decompose_ptr(ptr);
  return {shape.anchor, shape.anchor_offset};
}

static std::optional<i128> mem_intrin_bytes(fir::Instr instr) {
  if (!instr->args[2].is_constant_int()) {
    return std::nullopt;
  }
  const i128 count = instr->args[2].as_constant()->as_int();
  if (instr->is(fir::IntrinsicSubType::Memset)) {
    return count * static_cast<i128>(instr->args[1].get_type()->get_size());
  }
  return count * static_cast<i128>(instr->memcpy_elem_size());
}

// When the clobber of a load (or of a copy source) is a Memset or Memcpy that
// wrote all its bytes, the load sees the filled value or the copied from bytes.
// Replaced instructions stay until the end, the cached accesses still name
// them.
class MemIntrinForwarder {
 public:
  MemIntrinForwarder(fir::Context &ctx, fir::Function &f)
      : ctx(&ctx),
        cfg(AnalysisManager::cfg(f)),
        dom(AnalysisManager::dom(f)),
        info(AnalysisManager::mem_accesses(f)),
        mssa(AnalysisManager::memory_ssa(f)),
        aq(AnalysisManager::provenance(f)),
        walker(mssa, info, aq) {}

  bool run() {
    for (const auto &a : info.accesses()) {
      if (!dom.is_reachable(cfg.get_bb_id(a.instr->get_parent()))) {
        continue;
      }
      if (a.kind == MemAccess::Kind::Load && !a.ordered && a.loc.size != 0 &&
          a.instr->is(fir::InstrType::LoadInstr) && !a.instr->Volatile &&
          !a.instr->Atomic) {
        if (forward_load(a)) {
          dead.push_back(a.instr);
        }
      } else if (a.instr->is(fir::IntrinsicSubType::Memcpy)) {
        if (forward_memcpy_chain(a.instr)) {
          modified = true;
        }
      }
    }
    for (auto instr : dead) {
      instr.destroy();
    }
    return modified || !dead.empty();
  }

 private:
  bool dominates(fir::Instr a, fir::Instr b) const {
    auto ba = a->get_parent();
    auto bb = b->get_parent();
    if (ba == bb) {
      return ba->get_instr_id(a) < ba->get_instr_id(b);
    }
    return dom.strict_dominates(ba, bb);
  }

  // The fill or copy at state c if one instruction wrote all of the bytes
  // [want + size) before user.
  std::optional<fir::Instr> covering_writer(u32 c, const PtrShape &want,
                                            i128 size, fir::Instr user) const {
    const auto &ca = mssa[c];
    if (ca.kind != MemorySSA::Kind::Def || ca.ordered) {
      return std::nullopt;
    }
    auto def = ca.instr;
    if (!def->is(fir::InstrType::Intrinsic) ||
        (!def->is(fir::IntrinsicSubType::Memset) &&
         !def->is(fir::IntrinsicSubType::Memcpy)) ||
        def->Volatile) {
      return std::nullopt;
    }
    auto bytes = mem_intrin_bytes(def);
    if (!bytes || !dominates(def, user)) {
      return std::nullopt;
    }
    const auto have = split_ptr(def->args[0]);
    if (have.base != want.anchor || have.off > want.anchor_offset ||
        want.anchor_offset + size > have.off + *bytes) {
      return std::nullopt;
    }
    return def;
  }

  // The bytes of the copy source that correspond to [want + size) of the
  // destination.
  static MemLoc source_loc(fir::Instr copy, i128 rel, i128 size) {
    MemLoc loc;
    loc.ptr = decompose_ptr(copy->args[1]);
    loc.ptr.offset += static_cast<i64>(rel);
    loc.ptr.anchor_offset += static_cast<i64>(rel);
    loc.size = static_cast<u32>(size);
    return loc;
  }

  // Nothing wrote loc between the copy at state c and the state before user.
  bool source_unmodified(u32 c, fir::Instr user, const MemLoc &loc) {
    return !walker.clobbered_between(c, mssa.defining_access(user), loc);
  }

  // A load of bytes a memset or memcpy wrote is the filled value or a load of
  // the copied from bytes.
  bool forward_load(const MemAccess &load_acc) {
    auto load = load_acc.instr;
    auto ty = load->result_type();
    const i128 lsize = ty->get_size();
    if (lsize == 0) {
      return false;
    }
    const u32 c = walker.clobber(load);
    auto writer = covering_writer(c, load_acc.loc.ptr, lsize, load);
    if (!writer) {
      return false;
    }
    const fir::Instr def = *writer;
    const i128 rel =
        load_acc.loc.ptr.anchor_offset - split_ptr(def->args[0]).off;
    fir::Builder b{load};
    if (def->is(fir::IntrinsicSubType::Memcpy)) {
      const auto src = source_loc(def, rel, lsize);
      if (!source_unmodified(c, load, src)) {
        return false;
      }
      auto nl = b.build_load(ty, offset_ptr(*ctx, b, def->args[1], rel), false,
                             false);
      load->replace_all_uses(nl);
      return true;
    }
    auto val = def->args[1];
    const i128 elem = val.get_type()->get_size();
    if (elem != 1) {
      if (val.get_type() == ty && rel % elem == 0) {
        load->replace_all_uses(val);
        return true;
      }
      return false;
    }
    if (val.is_constant_int()) {
      const u64 byte = static_cast<u8>(val.as_constant()->as_int());
      u64 pattern = 0;
      for (i128 i = 0; i < std::min<i128>(lsize, 8); i++) {
        pattern |= byte << (8 * i);
      }
      if (ty->is_int() && lsize <= 8) {
        load->replace_all_uses(fir::ValueR{(*ctx)->get_constant_value(
            pattern, (*ctx)->get_int_type(lsize * 8))});
        return true;
      }
      if (ty->is_float() && ty->get_bitwidth() == 32) {
        load->replace_all_uses(fir::ValueR{(*ctx)->get_constant_value(
            std::bit_cast<f32>(static_cast<u32>(pattern)),
            (*ctx)->get_float_type(32))});
        return true;
      }
      if (ty->is_float() && ty->get_bitwidth() == 64) {
        load->replace_all_uses(fir::ValueR{(*ctx)->get_constant_value(
            std::bit_cast<f64>(pattern), (*ctx)->get_float_type(64))});
        return true;
      }
      return false;
    }
    if (ty->is_int() && lsize <= 8 && lsize >= 2) {
      auto wide = b.build_zext(val, ty);
      u64 splat = 0;
      for (i128 i = 0; i < lsize; i++) {
        splat |= u64{1} << (8 * i);
      }
      auto res =
          b.build_int_mul(wide, fir::ValueR{(*ctx)->get_constant_value(
                                    splat, (*ctx)->get_int_type(lsize * 8))});
      load->replace_all_uses(res);
      return true;
    }
    return false;
  }

  // memcpy(c, b) of bytes an earlier memcpy(b, a) or memset(b) wrote copies
  // from a directly (or fills c directly)
  bool forward_memcpy_chain(fir::Instr instr) {
    auto bytes = mem_intrin_bytes(instr);
    if (!bytes || instr->Volatile || *bytes <= 0 || *bytes >= UINT32_MAX) {
      return false;
    }
    MemLoc src_loc;
    src_loc.ptr = decompose_ptr(instr->args[1]);
    src_loc.size = static_cast<u32>(*bytes);
    const u32 c = walker.clobber_from(mssa.defining_access(instr), src_loc);
    auto writer = covering_writer(c, src_loc.ptr, *bytes, instr);
    if (!writer) {
      return false;
    }
    const fir::Instr def = *writer;
    const i128 rel = src_loc.ptr.anchor_offset - split_ptr(def->args[0]).off;
    fir::Builder b{instr};
    if (def->is(fir::IntrinsicSubType::Memset)) {
      auto val = def->args[1];
      if (val.get_type()->get_size() != 1) {
        return false;
      }
      b.build_intrinsic(instr->args[0], val,
                        fir::ValueR{(*ctx)->get_constant_int(*bytes, 64)},
                        fir::IntrinsicSubType::Memset, (*ctx)->get_void_type());
      dead.push_back(instr);
      return false;
    }
    const auto src = source_loc(def, rel, *bytes);
    MemLoc dst_loc;
    dst_loc.ptr = decompose_ptr(instr->args[0]);
    dst_loc.size = static_cast<u32>(*bytes);
    if (!source_unmodified(c, instr, src) ||
        aq.alias(dst_loc, src) != AliasResult::No) {
      return false;
    }
    instr.replace_arg(1, offset_ptr(*ctx, b, def->args[1], rel));
    return true;
  }

  fir::Context *ctx;
  CFG &cfg;
  Dominators &dom;
  MemAccessInfo &info;
  MemorySSA &mssa;
  AliasQuery aq;
  MemorySSAWalker walker;
  TVec<fir::Instr> dead;
  bool modified = false;
};

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
  auto bb = instr->get_parent();
  const size_t copy_pos = bb->get_instr_id(instr);
  size_t last_read = copy_pos;
  auto block_local_reads = [&](auto &&self, fir::ValueR ptr) -> bool {
    for (auto u : *ptr.get_uses()) {
      if (u.user == instr && u.argId == 0) {
        continue;
      }
      if (u.user->get_parent() != bb) {
        return false;
      }
      const size_t pos = bb->get_instr_id(u.user);
      if (pos < copy_pos) {
        return false;
      }
      last_read = std::max(last_read, pos);
      if (u.user->is(fir::InstrType::LoadInstr) && u.argId == 0) {
        continue;
      }
      if (u.user->is(fir::BinaryInstrSubType::PtrAdd) && u.argId == 0 &&
          self(self, u.user.result())) {
        continue;
      }
      return false;
    }
    return true;
  };
  if (!block_local_reads(block_local_reads, target_ptr_instr.result())) {
    return false;
  }
  for (size_t i = copy_pos + 1; i < last_read; i++) {
    auto between = bb->instructions[i];
    if (between->pot_modifies_mem() || between->has_pot_sideeffects()) {
      return false;
    }
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
  auto dst_ptr = instr->args[0];
  auto src_ptr = instr->args[1];
  const i128 max_len = max_vec_bytes(ctx);
  auto chunks = plan_chunks(size, max_len, !instr->Volatile);
  if (chunks.size() > MaxInlineChunks) {
    return false;
  }

  auto input_ty = guessType(src_ptr);
  auto output_ty = guessType(dst_ptr);
  const bool types_match =
      input_ty.typeless || output_ty.typeless ||
      (input_ty.type.is_valid() && output_ty.type.is_valid() &&
       input_ty.type->eql(*output_ty.type.get_raw_ptr()));
  fir::Builder b{instr};
  // a single scalar move keeps the type of the objects (floats, pointers)
  if (chunks.size() == 1 && size <= 8 && types_match) {
    fir::TypeR selected_type =
        input_ty.type.is_valid()
            ? input_ty.type
            : (output_ty.type.is_valid() ? output_ty.type
                                         : ctx->get_int_type(size * 8));
    if (selected_type->get_size() == static_cast<u64>(size)) {
      auto input = b.build_load(selected_type, src_ptr, false, instr->Volatile);
      b.build_store(dst_ptr, input, false, instr->Volatile);
      instr.destroy();
      return true;
    }
  }
  for (auto c : chunks) {
    auto ty = chunk_type(ctx, c.len);
    auto v = b.build_load(ty, offset_ptr(ctx, b, src_ptr, c.off), false,
                          instr->Volatile);
    b.build_store(offset_ptr(ctx, b, dst_ptr, c.off), v, false,
                  instr->Volatile);
  }
  instr.destroy();
  return true;
}

static bool simplify_memset(fir::Instr instr, fir::Context &ctx) {
  auto dst_ptr = instr->args[0];
  auto value = instr->args[1];
  auto val_ty = value.get_type();
  if (!instr->args[2].is_constant_int() || val_ty->is_vec()) {
    return false;
  }
  const i128 count = instr->args[2].as_constant()->as_int();
  const i128 elem = val_ty->get_size();
  if (count == 0) {
    instr.destroy();
    return true;
  }
  fir::Builder b{instr};
  if (elem != 1) {
    // a wide value is stored once per element
    if (count > static_cast<i128>(MaxInlineChunks)) {
      return false;
    }
    for (i128 i = 0; i < count; i++) {
      b.build_store(offset_ptr(ctx, b, dst_ptr, i * elem), value, false,
                    instr->Volatile);
    }
    instr.destroy();
    return true;
  }
  const i128 max_len = max_vec_bytes(ctx);
  auto chunks = plan_chunks(count, max_len, !instr->Volatile);
  if (chunks.size() > MaxInlineChunks) {
    return false;
  }
  const bool const_val = value.is_constant_int();
  const u64 byte =
      const_val ? static_cast<u8>(value.as_constant()->as_int()) : 0;
  // the value in all bytes of an i64
  std::optional<fir::ValueR> splat64;
  auto get_splat64 = [&]() {
    if (!splat64) {
      auto wide = b.build_zext(value, ctx->get_int_type(64));
      splat64 = b.build_int_mul(
          wide,
          fir::ValueR{ctx->get_constant_value(
              static_cast<u64>(0x0101010101010101ULL), ctx->get_int_type(64))});
    }
    return *splat64;
  };
  std::optional<fir::ValueR> vec_cache;
  i128 vec_cache_len = 0;
  for (auto c : chunks) {
    fir::ValueR chunk = value;
    if (c.len >= 16) {
      auto vec_ty = chunk_type(ctx, c.len);
      if (vec_cache_len != c.len) {
        vec_cache_len = c.len;
        if (const_val) {
          auto lane = ctx->get_constant_value(
              static_cast<u64>(byte * 0x0101010101010101ULL),
              ctx->get_int_type(64));
          IRVec<fir::ConstantValueR> lanes(static_cast<size_t>(c.len / 8),
                                           lane);
          vec_cache = fir::ValueR{ctx->get_constant_value(lanes, vec_ty)};
        } else {
          vec_cache = b.build_vbroadcast(get_splat64(), vec_ty);
        }
      }
      chunk = *vec_cache;
    } else if (c.len != 1) {
      auto ty = ctx->get_int_type(c.len * 8);
      if (const_val) {
        u64 pattern = byte * 0x0101010101010101ULL;
        if (c.len < 8) {
          pattern &= (1ULL << (c.len * 8)) - 1;
        }
        chunk = fir::ValueR{ctx->get_constant_value(pattern, ty)};
      } else if (c.len == 8) {
        chunk = get_splat64();
      } else {
        chunk = b.build_itrunc(get_splat64(), ty);
      }
    }
    b.build_store(offset_ptr(ctx, b, dst_ptr, c.off), chunk, false,
                  instr->Volatile);
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
  const i128 size = const_count
                        ? instr->args[2].as_constant()->as_int() *
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
  if (!const_count) {
    return false;
  }
  auto chunks = plan_chunks(size, max_vec_bytes(ctx), true);
  if (chunks.size() > MaxMoveChunks) {
    return false;
  }
  fir::Builder b{instr};
  TVec<fir::ValueR> vals;
  for (auto c : chunks) {
    vals.push_back(b.build_load(chunk_type(ctx, c.len),
                                offset_ptr(ctx, b, src_ptr, c.off), false,
                                false));
  }
  for (size_t i = 0; i < chunks.size(); i++) {
    b.build_store(offset_ptr(ctx, b, dst_ptr, chunks[i].off), vals[i], false,
                  false);
  }
  instr.destroy();
  return true;
}

// The first differing chunk decides the sign. Chunks are compared as big endian
// numbers. An overlapping tail chunk is fine, the shared bytes are equal.
static bool memcmp_ordered(fir::Instr instr, fir::Context &ctx, i128 size) {
  auto a = instr->args[0];
  auto b = instr->args[1];
  auto chunks = plan_chunks(size, 8, true);
  if (chunks.size() > MaxMemcmpChunks) {
    return false;
  }
  auto i32_ty = ctx->get_int_type(32);
  fir::Builder bld{instr};
  TVec<fir::ValueR> diffs;
  for (auto c : chunks) {
    auto ty = ctx->get_int_type(c.len * 8);
    fir::ValueR la =
        bld.build_load(ty, offset_ptr(ctx, bld, a, c.off), false, false);
    fir::ValueR lb =
        bld.build_load(ty, offset_ptr(ctx, bld, b, c.off), false, false);
    if (c.len > 1) {
      la = bld.build_intrinsic(la, fir::IntrinsicSubType::Bswap);
      lb = bld.build_intrinsic(lb, fir::IntrinsicSubType::Bswap);
    }
    auto gt = bld.build_zext(
        bld.build_int_cmp(la, lb, fir::ICmpInstrSubType::UGT), i32_ty);
    auto lt = bld.build_zext(
        bld.build_int_cmp(la, lb, fir::ICmpInstrSubType::ULT), i32_ty);
    diffs.push_back(bld.build_int_sub(gt, lt));
  }
  auto zero = fir::ValueR{ctx->get_constant_int(0, 32)};
  fir::ValueR res = diffs.back();
  for (size_t i = diffs.size() - 1; i > 0; i--) {
    auto same =
        bld.build_int_cmp(diffs[i - 1], zero, fir::ICmpInstrSubType::EQ);
    res = bld.build_select(i32_ty, same, res, diffs[i - 1]);
  }
  instr->replace_all_uses(res);
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
    auto la = bld.build_zext(
        bld.build_load(ctx->get_int_type(8), a, false, false), i32_ty);
    auto lb = bld.build_zext(
        bld.build_load(ctx->get_int_type(8), b, false, false), i32_ty);
    instr->replace_all_uses(bld.build_int_sub(la, lb));
    instr.destroy();
    return true;
  }
  bool eq_only = size <= MaxMemcmpEq;
  for (auto u : instr->uses) {
    const bool eq_zero_cmp = u.user->is(fir::InstrType::ICmp) && u.argId == 0 &&
                             (u.user->is(fir::ICmpInstrSubType::EQ) ||
                              u.user->is(fir::ICmpInstrSubType::NE)) &&
                             u.user->args[1].is_constant_int() &&
                             u.user->args[1].as_constant()->as_int() == 0;
    if (!eq_zero_cmp) {
      eq_only = false;
    }
  }
  if (!eq_only) {
    return memcmp_ordered(instr, ctx, size);
  }
  // chunks may overlap, this only reads
  std::optional<fir::ValueR> acc;
  auto add_chunk = [&](i128 off, i128 len) {
    auto ty = ctx->get_int_type(len * 8);
    auto la = bld.build_load(ty, offset_ptr(ctx, bld, a, off), false, false);
    auto lb = bld.build_load(ty, offset_ptr(ctx, bld, b, off), false, false);
    fir::ValueR diff =
        bld.build_binary_op(la, lb, fir::BinaryInstrSubType::Xor);
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
      *acc,
      fir::ValueR{ctx->get_constant_int(0, acc->get_type()->get_bitwidth())},
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

PreservedAnalysis IntrinSimplify::apply(fir::Context &ctx,
                                        fir::Function &func) {
  ZoneScopedNC("IntrinSimplify", COLOR_OPTIMF);

  bool modified = false;
  const bool has_writer =
      std::ranges::any_of(func.basic_blocks, [](fir::BasicBlock bb) {
        return std::ranges::any_of(bb->get_instrs(), [](fir::Instr i) {
          return i->is(fir::IntrinsicSubType::Memset) ||
                 i->is(fir::IntrinsicSubType::Memcpy);
        });
      });
  if (has_writer && MemIntrinForwarder{ctx, func}.run()) {
    modified = true;
    AnalysisManager::drop_memory(func);
  }
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

}  // namespace foptim::optim
