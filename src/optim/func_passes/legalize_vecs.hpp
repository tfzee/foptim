#pragma once
#include <fmt/base.h>
#include <fmt/core.h>

#include <limits>
#include <optional>

#include "config/compiler_config.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/function.hpp"
#include "ir/helpers.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/function_pass.hpp"
#include "utils/string.hpp"

namespace foptim::optim {

class LegalizeVecs final : public FunctionPass {
  // one global per distinct scalar float constant of a function
  using FloatPool = TMap<std::pair<u32, u64>, fir::Global>;

  // loads the constant from a new global right before instr
  fir::ValueR load_constant_global(fir::Context &ctx, fir::Instr instr,
                                   fir::ConstantValueR constant) {
    const auto size = (constant->type->get_bitwidth() + 7) / 8;
    // numbered per function so the name does not depend on the job order
    auto func = instr->get_parent()->get_parent();
    IRString name;
    fmt::format_to(std::back_inserter(name), "const_legalizeVec_{}_{}",
                   func->name.c_str(), func->n_vec_constants++);
    auto global = ctx->insert_global(std::move(name), size);
    global->is_constant = true;
    global->linkage = fir::Linkage::Internal;
    global->init_value = foptim::utils::IRAlloc<uint8_t>{}.allocate(size);
    memset(global->init_value, 0, size);

    convert_constant_init(global->init_value, constant, global);

    fir::Builder bb{instr};
    return bb.build_load(constant->type,
                         fir::ValueR{ctx->get_constant_value(global)}, false,
                         false);
  }

  // scalar float constants are read from memory, the alternative is a movabs
  // and a move into the vector register for every use
  bool make_scalar_float_global(fir::Context &ctx, fir::Instr instr,
                                u32 arg_indx, FloatPool &pool) {
    auto constant = instr->args[arg_indx].as_constant();
    const u32 bits = constant->type->get_bitwidth();
    const u64 pattern = bits == 32
                            ? std::bit_cast<u32>(constant->as_f32())
                            : std::bit_cast<u64>(constant->as_f64());
    // zero and all ones are made in the register (vxor, vpcmpeq)
    if (pattern == 0 ||
        pattern == (bits == 32 ? std::numeric_limits<u32>::max()
                               : std::numeric_limits<u64>::max())) {
      return false;
    }
    auto &global = pool[{bits, pattern}];
    if (global == nullptr) {
      const auto size = bits / 8;
      auto func = instr->get_parent()->get_parent();
      IRString name;
      fmt::format_to(std::back_inserter(name), "const_legalizeVec_{}_{}",
                     func->name.c_str(), func->n_vec_constants++);
      global = ctx->insert_global(std::move(name), size);
      global->is_constant = true;
      global->linkage = fir::Linkage::Internal;
      global->init_value = foptim::utils::IRAlloc<uint8_t>{}.allocate(size);
      memset(global->init_value, 0, size);
      convert_constant_init(global->init_value, constant, global);
    }
    fir::Builder bb{instr};
    auto load = bb.build_load(constant->type,
                              fir::ValueR{ctx->get_constant_value(global)},
                              false, false);
    instr.replace_arg(arg_indx, load);
    return true;
  }

  void make_constant_global(fir::Context &ctx, fir::Instr instr, u32 arg_indx) {
    auto load_val =
        load_constant_global(ctx, instr, instr->args[arg_indx].as_constant());
    instr.replace_arg(arg_indx, load_val);
  }

  // the matcher builds these with a vxor, no load from a constant needed
  static bool is_zero_vec(fir::ConstantValueR c) {
    for (const auto &m : c->as_vec().members) {
      const bool zero =
          (m->is_int() && m->as_int() == 0) ||
          (m->is_float() && m->as_f64() == 0.0 && !std::signbit(m->as_f64())) ||
          m->is_null();
      if (!zero) {
        return false;
      }
    }
    return true;
  }

  static void push_all_uses(TVec<fir::Instr> &worklist, fir::Instr instr) {
    for (auto u : instr->get_uses()) {
      worklist.push_back(u.user);
    }
  }

  // shift amount when every lane of a vector shifts by the same constant
  static std::optional<u64> splat_shift_amount(fir::ValueR v) {
    if (v.is_instr() && v.as_instr()->is(fir::VectorISubType::Broadcast)) {
      v = v.as_instr()->args[0];
    }
    if (v.is_constant() && v.as_constant()->is_int()) {
      return static_cast<u64>(v.as_constant()->as_int());
    }
    if (v.is_constant() && v.as_constant()->is_vec()) {
      const auto &members = v.as_constant()->as_vec().members;
      if (members.empty() || !members[0]->is_int()) {
        return std::nullopt;
      }
      for (const auto &m : members) {
        if (!m->is_int() || m->as_int() != members[0]->as_int()) {
          return std::nullopt;
        }
      }
      return static_cast<u64>(members[0]->as_int());
    }
    return std::nullopt;
  }

  // there are no byte shifts: shift the words and clear the bits that came
  // from the neighbouring byte, an arithmetic shift fills them with the sign
  // by xor and sub of the moved sign bit
  bool legalize_byte_shift(fir::Context &ctx, fir::Instr instr,
                           TVec<fir::Instr> &worklist) {
    using Sub = fir::BinaryInstrSubType;
    if (!(instr->is(Sub::Shl) || instr->is(Sub::Shr) || instr->is(Sub::AShr)) ||
        !instr->result_type()->is_vec()) {
      return false;
    }
    const auto vt = instr->result_type()->as_vec();
    const auto amount = splat_shift_amount(instr->args[1]);
    if (vt.bitwidth == 16 && amount && *amount < 16 &&
        instr->args[1].is_constant()) {
      // the matcher takes the amount as an immediate, a constant vector
      // would be moved into a global
      fir::Builder wb{instr};
      instr.replace_arg(
          1, wb.build_vbroadcast(fir::ValueR{ctx->get_constant_int(
                                     static_cast<i128>(*amount), 16)},
                                 instr->args[1].get_type()));
      return true;
    }
    if (vt.bitwidth != 8 || vt.member_number % 2 != 0) {
      return false;
    }
    // one value in a register for all lanes, the masks are made from it
    fir::ValueR var_amount;
    bool has_var_amount = false;
    if (!amount && instr->args[1].is_instr() &&
        instr->args[1].as_instr()->is(fir::VectorISubType::Broadcast) &&
        !instr->args[1].as_instr()->args[0].is_constant()) {
      var_amount = instr->args[1].as_instr()->args[0];
      has_var_amount = true;
    } else if (!amount || *amount >= 8) {
      return false;
    }
    fir::Builder b{instr};
    if (!has_var_amount && *amount == 0) {
      push_all_uses(worklist, instr);
      instr->replace_all_uses(instr->args[0]);
      instr.destroy();
      return true;
    }
    const auto byte_ty = instr->result_type();
    const auto word_ty =
        ctx->get_vec_type(vt.type, 16, static_cast<u16>(vt.member_number / 2));
    const auto i16_ty = ctx->get_int_type(16);
    const auto i8_ty = ctx->get_int_type(8);
    const auto splat = [&](u64 value, u32 bits, fir::TypeR ty) {
      return b.build_vbroadcast(
          fir::ValueR{ctx->get_constant_int(static_cast<i128>(value), bits)},
          ty);
    };
    const bool left = instr->is(Sub::Shl);
    // amount for the word shift and the byte masks as a vector each
    fir::ValueR word_amount;
    fir::ValueR keep_mask;
    fir::ValueR sign_mask;
    if (has_var_amount) {
      auto wide = b.build_zext(var_amount, i16_ty);
      word_amount = b.build_vbroadcast(wide, word_ty);
      const auto scalar_mask = [&](u64 base, bool shift_left) {
        auto c =
            fir::ValueR{ctx->get_constant_int(static_cast<i128>(base), 16)};
        auto m = shift_left ? b.build_shl(c, wide) : b.build_lshr(c, wide);
        return b.build_vbroadcast(b.build_itrunc(m, i8_ty), byte_ty);
      };
      keep_mask = scalar_mask(0xFF, left);
      if (instr->is(Sub::AShr)) {
        sign_mask = scalar_mask(0x80, false);
      }
    } else {
      word_amount = splat(*amount, 16, word_ty);
      keep_mask = splat(left ? (0xFFu << *amount) & 0xFFu : 0xFFu >> *amount, 8,
                        byte_ty);
      if (instr->is(Sub::AShr)) {
        sign_mask = splat(0x80u >> *amount, 8, byte_ty);
      }
    }
    auto words = b.build_conversion_op(instr->args[0], word_ty,
                                       fir::ConversionSubType::BitCast);
    auto shifted_words = left ? b.build_shl(words, word_amount)
                              : b.build_lshr(words, word_amount);
    auto bytes = b.build_conversion_op(shifted_words, byte_ty,
                                       fir::ConversionSubType::BitCast);
    auto res = b.build_binary_op(bytes, keep_mask, Sub::And);
    if (instr->is(Sub::AShr)) {
      auto flipped = b.build_binary_op(res, sign_mask, Sub::Xor);
      res = b.build_int_sub(flipped, sign_mask);
    }
    push_all_uses(worklist, instr);
    instr->replace_all_uses(res);
    instr.destroy();
    return true;
  }

  // shuffles of byte and word lanes with a constant mask become one pshufb per
  // source (zeroing the lanes that come from the other one) and an or. pshufb
  // stays inside its 128 bit lane, a mask that crosses lanes is left alone
  bool legalize_narrow_shuffle(fir::Context &ctx, fir::Instr instr,
                               TVec<fir::Instr> &worklist) {
    if (!instr->is(fir::VectorISubType::Shuffle) ||
        !instr->args[2].is_constant() ||
        !instr->args[2].as_constant()->is_vec()) {
      return false;
    }
    const auto res_ty = instr->result_type()->as_vec();
    const auto src_ty = instr->args[0].get_type()->as_vec();
    if (res_ty.type != fir::VectorType::SubType::Integer ||
        (res_ty.bitwidth != 8 && res_ty.bitwidth != 16)) {
      return false;
    }
    const u32 lane_bytes = res_ty.bitwidth / 8;
    const u32 src_bytes = src_ty.member_number * lane_bytes;
    const u32 res_bytes = res_ty.member_number * lane_bytes;
    if ((src_bytes != 16 && src_bytes != 32) || res_bytes > src_bytes) {
      return false;
    }
    const auto &mask = instr->args[2].as_constant()->as_vec().members;
    constexpr i128 Zero = 0x80;
    TVec<i128> ctrl_a(src_bytes, Zero);
    TVec<i128> ctrl_b(src_bytes, Zero);
    bool use_a = false;
    bool use_b = false;
    for (u32 j = 0; j < mask.size(); j++) {
      const i128 m = mask[j]->as_int();
      if (m < 0) {
        continue;
      }
      const bool second = m >= src_ty.member_number;
      const u32 lane = static_cast<u32>(m) % src_ty.member_number;
      for (u32 t = 0; t < lane_bytes; t++) {
        const u32 pos = j * lane_bytes + t;
        const u32 idx = lane * lane_bytes + t;
        if (idx / 16 != pos / 16) {
          return false;
        }
        (second ? ctrl_b : ctrl_a)[pos] = idx % 16;
      }
      (second ? use_b : use_a) = true;
    }
    use_a |= !use_b;

    fir::Builder b{instr};
    const auto byte_ty = ctx->get_vec_type(fir::VectorType::SubType::Integer, 8,
                                           static_cast<u16>(src_bytes));
    const auto shuffle_with = [&](fir::ValueR src, const TVec<i128> &ctrl) {
      IRVec<fir::ConstantValueR> members;
      for (const i128 c : ctrl) {
        members.push_back(ctx->get_constant_int(c, 8));
      }
      auto bytes = lane_bytes == 1
                       ? src
                       : b.build_conversion_op(src, byte_ty,
                                               fir::ConversionSubType::BitCast);
      auto ctrl_vec =
          fir::ValueR{ctx->get_constant_value(std::move(members), byte_ty)};
      return b.build_vector_op(bytes, ctrl_vec, byte_ty,
                               fir::VectorISubType::ByteShuffle);
    };
    fir::ValueR res;
    if (use_a) {
      res = shuffle_with(instr->args[0], ctrl_a);
    }
    if (use_b) {
      auto from_b = shuffle_with(instr->args[1], ctrl_b);
      res = use_a ? b.build_binary_op(res, from_b, fir::BinaryInstrSubType::Or)
                  : from_b;
    }
    if (res_bytes == src_bytes) {
      if (lane_bytes != 1) {
        res = b.build_conversion_op(res, instr->result_type(),
                                    fir::ConversionSubType::BitCast);
      }
    } else {
      auto wide_ty = instr->args[0].get_type();
      if (lane_bytes != 1) {
        res = b.build_conversion_op(res, wide_ty,
                                    fir::ConversionSubType::BitCast);
      }
      res = b.build_vector_op(res, instr->result_type(),
                              fir::VectorISubType::ExtractLow);
    }
    push_all_uses(worklist, instr);
    instr->replace_all_uses(res);
    instr.destroy();
    return true;
  }

  bool legalize(fir::Context &ctx, fir::Instr instr, TVec<fir::Instr> &worklist,
                FloatPool &pool) {
    if (legalize_byte_shift(ctx, instr, worklist)) {
      return true;
    }
    if (legalize_narrow_shuffle(ctx, instr, worklist)) {
      return true;
    }
    // constant vector operands get moved into globals, whatever else happens
    bool changed = false;
    if ((instr->is(fir::BinaryInstrSubType::FloatAdd) ||
         instr->is(fir::BinaryInstrSubType::FloatSub) ||
         instr->is(fir::BinaryInstrSubType::FloatMul) ||
         instr->is(fir::BinaryInstrSubType::FloatDiv)) &&
        !instr->result_type()->is_vec()) {
      for (u32 arg_indx = 0; arg_indx < 2; arg_indx++) {
        if (instr->args[arg_indx].is_constant_float()) {
          changed |= make_scalar_float_global(ctx, instr, arg_indx, pool);
        }
      }
    }
    for (size_t arg_indx = 0; arg_indx < instr->args.size(); arg_indx++) {
      auto arg = instr->args[arg_indx];
      // the matcher reads the mask of a shuffle as an immediate
      if (arg_indx == 2 && instr->is(fir::VectorISubType::Shuffle)) {
        continue;
      }
      // replace vec with reference to global variable
      if (arg.is_constant() && arg.as_constant()->is_vec() &&
          !is_zero_vec(arg.as_constant())) {
        make_constant_global(ctx, instr, arg_indx);
        changed = true;
      }
    }

    // the same for constants passed as block arguments of a branch
    for (size_t bb_id = 0; bb_id < instr->bbs.size(); bb_id++) {
      for (size_t i = 0; i < instr->bbs[bb_id].args.size(); i++) {
        auto arg = instr->bbs[bb_id].args[i];
        if (arg.is_constant() && arg.as_constant()->is_vec() &&
            !is_zero_vec(arg.as_constant())) {
          auto load_val = load_constant_global(ctx, instr, arg.as_constant());
          instr.replace_bb_arg(bb_id, i, load_val);
          changed = true;
        }
      }
    }

    // fmax can never take immediate even for singular values
    if (instr->is(fir::IntrinsicSubType::FMax) ||
        instr->is(fir::IntrinsicSubType::FMin)) {
      if (instr->args[0].is_constant_float()) {
        make_constant_global(ctx, instr, 0);
        return true;
      }
      if (instr->args[1].is_constant_float()) {
        make_constant_global(ctx, instr, 1);
        return true;
      }
    }

    if ((instr->is(fir::BinaryInstrSubType::FloatAdd) ||
         instr->is(fir::BinaryInstrSubType::FloatSub) ||
         instr->is(fir::BinaryInstrSubType::FloatMul) ||
         instr->is(fir::BinaryInstrSubType::FloatDiv) ||
         instr->is(fir::InstrType::FCmp)) &&
        instr->result_type()->is_vec()) {
      if (instr->args[0].is_constant_float()) {
        make_constant_global(ctx, instr, 0);
        return true;
      }
      if (instr->args[1].is_constant_float()) {
        make_constant_global(ctx, instr, 1);
        return true;
      }
    }

    if (instr->is(fir::IntrinsicSubType::FAbs) &&
        instr->result_type()->is_vec()) {
      fir::Builder b{instr};
      auto width = instr->result_type()->as_vec().bitwidth;
      auto f_type = ctx->get_float_type(width);
      ASSERT(width == 64 || width == 32);
      fir::ValueR broad{};
      if (width == 64) {
        broad = b.build_vbroadcast(
            fir::ValueR{ctx->get_constant_value(
                std::bit_cast<f64>(static_cast<u64>(0x7fffffffffffffff)),
                f_type)},
            instr->result_type());
      } else {
        broad = b.build_vbroadcast(
            fir::ValueR{ctx->get_constant_value(
                std::bit_cast<f32>(static_cast<u32>(0x7fffffff)), f_type)},
            instr->result_type());
      }
      auto r = b.build_binary_op(instr->args[0], broad,
                                 fir::BinaryInstrSubType::And);
      push_all_uses(worklist, instr);
      instr->replace_all_uses(r);
      instr.destroy();
      return true;
    }
    // TODO: is this check for avx512 sufficient/correct?
    if (instr->is(fir::VectorISubType::Broadcast)) {
      // TODO: this shouldnt realy be a thing ??
      if (instr->args[0].is_constant_float()) {
        make_constant_global(ctx, instr, 0);
        return true;
      }
      if (!ctx.config->target.features.avx512f &&
          instr->result_type()->get_bitwidth() >= 512) {
        // auto funcy = instr->get_parent()->get_parent().func;
        // fmt::println("{:cd}", *funcy);
        // fmt::println("{}", instr);
        fir::Builder buh{instr};
        auto old_type = instr->result_type()->as_vec();
        auto new_type = ctx->get_vec_type(old_type.type, old_type.bitwidth,
                                          old_type.member_number / 2);
        auto a1 = buh.build_vbroadcast(instr->args[0], new_type);
        auto a2 = buh.build_vbroadcast(instr->args[0], new_type);
        auto r = buh.build_vector_op(a1, a2, instr->result_type(),
                                     fir::VectorISubType::Concat);
        push_all_uses(worklist, instr);
        instr->replace_all_uses(r);
        instr.destroy();
        // fmt::println("{:cd}", *funcy);
        // TODO("split");
        return true;
      }
    }
    // TODO: is this check for avx512 sufficient/correct?
    if (instr->is(fir::ReduceInstrSubType::Add) &&
        !ctx.config->target.features.avx512f &&
        instr->args[0].get_type()->get_bitwidth() >= 512) {
      fir::Builder buh{instr};
      auto out_type = instr->result_type();
      auto old_type = instr->args[0].get_type()->as_vec();
      auto new_type = ctx->get_vec_type(old_type.type, old_type.bitwidth,
                                        old_type.member_number / 2);
      auto a_low = buh.build_vector_op(instr->args[0], new_type,
                                       fir::VectorISubType::ExtractLow);
      auto a_high = buh.build_vector_op(instr->args[0], new_type,
                                        fir::VectorISubType::ExtractHigh);
      auto a1 = buh.build_reduce(a_low, fir::ReduceInstrSubType::Add);
      auto a2 = buh.build_reduce(a_high, fir::ReduceInstrSubType::Add);
      auto r = out_type->is_float() ? buh.build_float_add(a1, a2)
                                    : buh.build_int_add(a1, a2);
      push_all_uses(worklist, instr);
      instr->replace_all_uses(r);
      instr.destroy();
      return true;
    }
    return changed;
  }

 public:
  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    TVec<fir::Instr> worklist;
    FloatPool pool;
    // TODO(PERF): do prefiltering
    for (auto bb : func.basic_blocks) {
      for (auto instruction : bb->instructions) {
        worklist.push_back(instruction);
      }
    }

    bool modified = false;
    while (!worklist.empty()) {
      auto curr = worklist.back();
      worklist.pop_back();
      modified |= legalize(ctx, curr, worklist, pool);
    }
    return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
  }
};
}  // namespace foptim::optim
