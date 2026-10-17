#pragma once
#include <fmt/base.h>

#include "../function_pass.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types_ref.hpp"
#include "optim/analysis/analysis_manager.hpp"
#include "optim/helper/helper.hpp"
#include "utils/todo.hpp"

namespace foptim::optim {

class LLVMInstrinsicLowering final : public FunctionPass {
  bool assume_cstdlib = false;

  // the last operand of llvm.memset/memcpy/memmove is the volatile flag
  static bool is_volatile_call(fir::Instr instr) {
    return instr->Volatile ||
           (instr->args.size() > 4 && instr->args[4].is_constant_int() &&
            instr->args[4].as_constant()->as_int() != 0);
  }

public:
  void handle_memset(fir::Instr instr, fir::Function &func,
                     fir::FunctionR /*callee*/) {
    auto *ctx = func.ctx;
    fir::Builder bb{instr};
    auto target_ptr = instr->args[1];
    auto value = instr->args[2];
    auto size = instr->args[3];
    if (size.get_type()->get_bitwidth() < 64) {
      size = bb.build_zext(size, ctx->get_int_type(64));
    }
    // the value is a byte so the count is in bytes
    auto memset_instr =
        bb.build_intrinsic(target_ptr, value, size,
                           fir::IntrinsicSubType::Memset, ctx->get_void_type());
    memset_instr.as_instr()->Volatile = is_volatile_call(instr);
    instr.destroy();
  }

  void handle_is_fpclass(fir::Instr instr, fir::Function &func,
                         fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto *ctx = func.ctx;
    auto val = instr->args[1];
    auto mode_a = instr->args[2];
    ASSERT(mode_a.is_constant());
    auto mode_c = mode_a.as_constant();
    ASSERT(mode_c->is_int());
    auto mode = mode_c->as_int();
    if (mode == 0) {
      instr->replace_all_uses(fir::ValueR{ctx->get_constant_int(0, 8)});
      instr.destroy();
      return;
    }

    // some setup
    auto float_type = val.get_type();
    uint32_t width = float_type->get_bitwidth();
    auto int_val = bb.build_conversion_op(
        val, ctx->get_int_type(float_type->get_bitwidth()),
        fir::ConversionSubType::BitCast);
    uint32_t mant_bits = (width == 32) ? 23 : 52;
    uint32_t exp_bits = (width == 32) ? 8 : 11;
    uint64_t exp_mask = (1ULL << exp_bits) - 1;
    uint64_t mant_mask = (1ULL << mant_bits) - 1;
    auto mant = bb.build_binary_op(
        int_val, fir::ValueR{ctx->get_constant_int(mant_mask, width)},
        fir::BinaryInstrSubType::And);
    auto exp = bb.build_binary_op(
        bb.build_binary_op(int_val,
                           fir::ValueR{ctx->get_constant_int(mant_bits, width)},
                           fir::BinaryInstrSubType::Shr),
        fir::ValueR{ctx->get_constant_int(exp_mask, width)},
        fir::BinaryInstrSubType::And);
    auto zero = fir::ValueR{ctx->get_constant_int(0, width)};
    auto exp_is_zero = bb.build_int_cmp(exp, zero, fir::ICmpInstrSubType::EQ);
    auto max_exp = fir::ValueR{ctx->get_constant_int(exp_mask, width)};

    auto mant_not_zero =
        bb.build_int_cmp(mant, zero, fir::ICmpInstrSubType::NE);

    // 0 Signaling NaN
    // 1 Quiet NaN
    // 2 Negative infinity
    // 3 Negative normal
    // 4 Negative subnormal
    // 5 Negative zero
    // 6 Positive zero
    // 7 Positive subnormal
    // 8 Positive normal
    // 9 Positive infinity

    // get components (Sign, Exponent, Mantissa)
    auto mant_is_zero = bb.build_int_cmp(mant, zero, fir::ICmpInstrSubType::EQ);
    auto exp_is_max = bb.build_int_cmp(exp, max_exp, fir::ICmpInstrSubType::EQ);
    auto is_zero = bb.build_binary_op(exp_is_zero, mant_is_zero,
                                      fir::BinaryInstrSubType::And);
    auto is_inf = bb.build_binary_op(exp_is_max, mant_is_zero,
                                     fir::BinaryInstrSubType::And);
    auto is_nan = bb.build_binary_op(exp_is_max, mant_not_zero,
                                     fir::BinaryInstrSubType::And);
    auto sign_bit = bb.build_binary_op(
        int_val, fir::ValueR{ctx->get_constant_int(width - 1, 32)},
        fir::BinaryInstrSubType::Shr);
    sign_bit = bb.build_itrunc(sign_bit, ctx->get_int_type(1));
    auto is_neg =
        bb.build_int_cmp(sign_bit, fir::ValueR{ctx->get_constant_int(0, width)},
                         fir::ICmpInstrSubType::NE);
    auto is_pos =
        bb.build_int_cmp(sign_bit, fir::ValueR{ctx->get_constant_int(0, width)},
                         fir::ICmpInstrSubType::EQ);

    // to distinguish +0/-0 and signs of Inf/Normal, we check the sign
    // bit

    foptim::TVec<fir::ValueR> checks;

    auto add_check = [&](bool condition, fir::ValueR v) {
      if (condition) {
        checks.push_back(v);
      }
    };

    // NaN checks (Bit 0 1)
    auto qnan_bit =
        fir::ValueR{ctx->get_constant_int(1ULL << (mant_bits - 1), width)};
    auto is_qnan_bit_set =
        bb.build_binary_op(mant, qnan_bit, fir::BinaryInstrSubType::And);
    auto is_qnan_bit_set_bool =
        bb.build_int_cmp(is_qnan_bit_set, zero, fir::ICmpInstrSubType::NE);
    auto is_snan = bb.build_binary_op(
        is_nan,
        bb.build_unary_op(is_qnan_bit_set_bool, fir::UnaryInstrSubType::Not),
        fir::BinaryInstrSubType::And);
    auto is_qnan = bb.build_binary_op(is_nan, is_qnan_bit_set_bool,
                                      fir::BinaryInstrSubType::And);

    if ((mode & 0x001) != 0) {
      add_check(true, is_snan);
    }
    if ((mode & 0x002) != 0) {
      add_check(true, is_qnan);
    }

    // infinity (Bit 2 9)
    if ((mode & 0x004) != 0) {
      add_check(true, bb.build_binary_op(is_inf, sign_bit,
                                         fir::BinaryInstrSubType::And)); // -Inf
    }
    if ((mode & 0x200) != 0) {
      add_check(true, bb.build_binary_op(is_inf, is_pos,
                                         fir::BinaryInstrSubType::And)); // +Inf
    }

    // zero (Bit 5 6)
    if ((mode & 0x020) != 0) {
      add_check(true, bb.build_binary_op(is_zero, is_neg,
                                         fir::BinaryInstrSubType::And)); // -0
    }
    if ((mode & 0x040) != 0) {
      add_check(true, bb.build_binary_op(is_zero, is_pos,
                                         fir::BinaryInstrSubType::And)); // +0
    }

    // normal and subnormal (Bits 3 4 7 8)
    // aavalue is "normal" if it's not zero, nan, inf, or subnormal.
    auto is_subnormal = bb.build_binary_op(exp_is_zero, mant_not_zero,
                                           fir::BinaryInstrSubType::And);

    // normal: exponent is NOT all 0s and exponent is not all 1
    auto exp_not_zero = bb.build_int_cmp(exp, zero, fir::ICmpInstrSubType::NE);
    auto exp_not_max =
        bb.build_int_cmp(exp, max_exp, fir::ICmpInstrSubType::NE);
    auto is_normal = bb.build_binary_op(exp_not_zero, exp_not_max,
                                        fir::BinaryInstrSubType::And);

    if ((mode & 0x008) != 0) {
      add_check(true,
                bb.build_binary_op(is_normal, is_neg,
                                   fir::BinaryInstrSubType::And)); // -Normal
    }
    if ((mode & 0x100) != 0) {
      add_check(true,
                bb.build_binary_op(is_normal, is_pos,
                                   fir::BinaryInstrSubType::And)); // +Normal
    }
    if ((mode & 0x010) != 0) {
      add_check(true,
                bb.build_binary_op(is_subnormal, is_neg,
                                   fir::BinaryInstrSubType::And)); // -Subnormal
    }
    if ((mode & 0x080) != 0) {
      add_check(true,
                bb.build_binary_op(is_subnormal, is_pos,
                                   fir::BinaryInstrSubType::And)); // +Subnormal
    }

    ASSERT(!checks.empty());
    fir::ValueR final_res = checks[0];
    for (size_t i = 1; i < checks.size(); ++i) {
      final_res =
          bb.build_binary_op(final_res, checks[i], fir::BinaryInstrSubType::Or);
    }
    instr->replace_all_uses(final_res);
    instr.destroy();
  }

  void handle_fmuladd(fir::Instr instr, fir::Function & /*func*/,
                      fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto mul_1 = instr->args[1];
    auto mul_2 = instr->args[2];
    auto add = instr->args[3];

    // fmuladd(-a, b, c) is better as `c - a*b`
    auto is_neg = [](fir::ValueR v) {
      return v.is_instr() && v.as_instr()->is(fir::UnaryInstrSubType::FloatNeg);
    };
    if (is_neg(mul_1) || is_neg(mul_2)) {
      if (is_neg(mul_1)) {
        mul_1 = mul_1.as_instr()->args[0];
      } else {
        mul_2 = mul_2.as_instr()->args[0];
      }
      auto mul_res = bb.build_float_mul(mul_1, mul_2);
      auto result = bb.build_float_sub(add, mul_res);
      instr->replace_all_uses(result);
      instr.destroy();
      return;
    }

    auto mul_res = bb.build_float_mul(mul_1, mul_2);
    auto result = bb.build_float_add(mul_res, add);
    instr->replace_all_uses(result);
    instr.destroy();
  }

  // llvm.memcpy(dst, src, len, volatile) becomes the memcpy intrinsic with a
  // byte count
  void handle_memcpy(fir::Instr instr, fir::Function &func,
                     fir::FunctionR /*callee*/) {
    auto *ctx = func.ctx;
    fir::Builder bb{instr};
    auto dst_ptr = instr->args[1];
    auto src_ptr = instr->args[2];
    auto size = instr->args[3];
    if (size.get_type()->get_bitwidth() < 64) {
      size = bb.build_zext(size, ctx->get_int_type(64));
    }
    auto memcpy_instr = bb.build_intrinsic(
        dst_ptr, src_ptr, size, fir::IntrinsicSubType::Memcpy,
        ctx->get_void_type());
    memcpy_instr.as_instr()->extra_type = ctx->get_int_type(8);
    memcpy_instr.as_instr()->Volatile = is_volatile_call(instr);
    instr.destroy();
  }

  // llvm.memmove(dst, src, len, volatile) becomes the memmove intrinsic with a
  // byte count
  void handle_memmove(fir::Instr instr, fir::Function &func,
                      fir::FunctionR /*callee*/) {
    auto *ctx = func.ctx;
    fir::Builder bb{instr};
    auto size = instr->args[3];
    if (size.get_type()->get_bitwidth() < 64) {
      size = bb.build_zext(size, ctx->get_int_type(64));
    }
    auto memmove_instr = bb.build_intrinsic(
        instr->args[1], instr->args[2], size, fir::IntrinsicSubType::Memmove,
        ctx->get_void_type());
    memmove_instr.as_instr()->extra_type = ctx->get_int_type(8);
    memmove_instr.as_instr()->Volatile = is_volatile_call(instr);
    instr.destroy();
  }
  // memmove and memcmp calls become the intrinsics (and calls again at the
  // very end), so they can be folded in between
  bool handle_libc_call(fir::Instr instr, fir::Function &func,
                        fir::FunctionR callee) {
    auto *ctx = func.ctx;
    const bool is_memmove = callee.func->name == "memmove";
    const bool is_memcmp = callee.func->name == "memcmp";
    if ((!is_memmove && !is_memcmp) || instr->args.size() != 4 ||
        !instr->args[1].get_type()->is_ptr() ||
        !instr->args[2].get_type()->is_ptr() ||
        !instr->args[3].get_type()->is_int() ||
        instr->args[3].get_type()->get_bitwidth() != 64) {
      return false;
    }
    fir::Builder bb{instr};
    if (is_memmove) {
      auto mm = bb.build_intrinsic(instr->args[1], instr->args[2],
                                   instr->args[3],
                                   fir::IntrinsicSubType::Memmove,
                                   ctx->get_void_type());
      mm.as_instr()->extra_type = ctx->get_int_type(8);
      instr->replace_all_uses(instr->args[1]);
    } else {
      if (!instr.get_type()->is_int() || instr.get_type()->get_bitwidth() != 32) {
        return false;
      }
      auto cmp = bb.build_intrinsic(instr->args[1], instr->args[2],
                                    instr->args[3],
                                    fir::IntrinsicSubType::Memcmp,
                                    ctx->get_int_type(32));
      instr->replace_all_uses(cmp);
    }
    instr.destroy();
    return true;
  }

  void handle_trap(fir::Instr instr, fir::Function &funcy,
                   fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto *ctx = funcy.ctx;
    auto abort_func = ctx->get_function("abort");
    auto void_type = ctx->get_void_type();

    bb.build_call(fir::ValueR{ctx->get_constant_value(abort_func)},
                  abort_func->func_ty, void_type, {});
    instr.destroy();
  }

  void handle_lifetime(fir::Instr instr, fir::Function & /*func*/,
                       fir::FunctionR /*callee*/) {
    instr.destroy();
  }

  void handle_fabs(fir::Instr instr, fir::Function & /*funcy*/,
                   fir::FunctionR /*callee*/) {
    // auto width = instr.get_type()->as_float();
    fir::Builder bb{instr};
    // auto *ctx = funcy.ctx;
    // const auto *func_name = "INVALID_FUNC_NAME";

    // if (width == 64) {
    //   func_name = "foptim.abs.f64";
    // } else if (width == 32) {
    //   func_name = "foptim.abs.f32";
    // } else {
    //   fmt::println("{}", instr);
    //   TODO("IMPL");
    // }

    // auto func = ctx->get_function(func_name);
    // auto ret_type = instr.get_type();
    // foptim::fir::ValueR args[1] = {instr->args[1]};
    // auto res =
    // bb.build_call(fir::ValueR{ctx->get_constant_value(func)},
    //                          func->func_ty, ret_type, args);
    auto res = bb.build_fabs(instr->args[1]);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_abs(fir::Instr instr, fir::Function & /*funcy*/,
                  fir::FunctionR /*callee*/) {
    // auto width = instr.get_type()->as_int();
    fir::Builder bb{instr};
    // auto *ctx = funcy.ctx;
    // const auto *func_name = "INVALID_FUNC_NAME";

    // if (width == 64) {
    //   func_name = "foptim.abs.i64";
    // } else if (width == 32) {
    //   func_name = "foptim.abs.i32";
    // } else {
    //   fmt::println("{}", instr);
    //   TODO("IMPL");
    // }

    // auto func = ctx->get_function(func_name);
    // auto ret_type = instr.get_type();
    // foptim::fir::ValueR args[1] = {instr->args[1]};
    // auto res =
    // bb.build_call(fir::ValueR{ctx->get_constant_value(func)},
    //                          func->func_ty, ret_type, args);
    auto res = bb.build_abs(instr->args[1]);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_is_constant(fir::Instr instr, fir::Function &funcy,
                          fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto res =
        bb.build_intrinsic(instr->args[1], fir::IntrinsicSubType::IsConstant,
                           funcy.ctx->get_int_type(1));
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_umul_with_overflow(fir::Instr instr, fir::Function & /*funcy*/,
                                 fir::FunctionR /*callee*/) {
    // auto width = instr.get_type()->as_struct().elems[0].ty->as_int();
    fir::Builder bb{instr};

    auto mul_result = bb.build_int_mul(instr->args[1], instr->args[2]);
    auto overflow_result = bb.build_int_cmp(instr->args[1], instr->args[2],
                                            fir::ICmpInstrSubType::MulOverflow);

    // annoying copy
    auto uses = instr->uses;
    for (auto use : uses) {
      ASSERT(use.user->is(fir::InstrType::ExtractValue));
      ASSERT(use.user->args.size() == 2);
      ASSERT(use.argId == 0);
      auto index = use.user->args[1].as_constant()->as_int();
      if (index == 0) {
        use.user->replace_all_uses(mul_result);
        use.user.destroy();
      } else if (index == 1) {
        use.user->replace_all_uses(overflow_result);
        use.user.destroy();
      } else {
        UNREACH();
      }
    }
    instr.destroy();
  }

  void handle_uadd_with_overflow(fir::Instr instr, fir::Function & /*funcy*/,
                                 fir::FunctionR /*callee*/) {
    // auto width = instr.get_type()->as_struct().elems[0].ty->as_int();
    fir::Builder bb{instr};

    auto add_result = bb.build_int_add(instr->args[1], instr->args[2]);
    auto overflow_result = bb.build_int_cmp(instr->args[1], instr->args[2],
                                            fir::ICmpInstrSubType::AddOverflow);

    // annoying copy
    auto uses = instr->uses;
    for (auto use : uses) {
      ASSERT(use.user->is(fir::InstrType::ExtractValue));
      ASSERT(use.user->args.size() == 2);
      ASSERT(use.argId == 0);
      auto index = use.user->args[1].as_constant()->as_int();
      if (index == 0) {
        use.user->replace_all_uses(add_result);
        use.user.destroy();
      } else if (index == 1) {
        use.user->replace_all_uses(overflow_result);
        use.user.destroy();
      } else {
        UNREACH();
      }
    }
    instr.destroy();
  }

  void handle_expect(fir::Instr instr, fir::Function & /*funcy*/,
                     fir::FunctionR /*callee*/) {
    instr->replace_all_uses(instr->args[1]);
    instr.destroy();
  }

  void handle_obj_size(fir::Instr instr, fir::Function &funcy,
                       fir::FunctionR /*callee*/) {
    auto *ctx = funcy.ctx;
    auto ret_type = instr->args[2].as_constant()->as_int();
    instr->replace_all_uses(fir::ValueR{
        ctx->get_constant_value(ret_type == 0 ? -1 : 0, instr->get_type())});
    instr.destroy();
  }

  void handle_fexp(fir::Instr instr, fir::Function &funcy,
                   fir::FunctionR /*callee*/) {
    auto *ctx = funcy.ctx;
    instr.replace_arg(
        0, fir::ValueR{ctx->get_constant_value(ctx->get_function("exp"))});
  }

  void handle_ctlz(fir::Instr instr, fir::Function & /*funcy*/,
                   fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto res = bb.build_ctlz(instr->args[1], instr->args[2]);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_cttz(fir::Instr instr, fir::Function & /*funcy*/,
                   fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto res = bb.build_cttz(instr->args[1], instr->args[2]);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_va(fir::Instr instr, fir::Function & /*funcy*/,
                 fir::FunctionR /*callee*/, bool is_start) {
    fir::Builder bb{instr};
    auto res = fir::ValueR{};
    if (is_start) {
      res = bb.build_va_start(instr->args[1]);
    } else {
      res = bb.build_va_end(instr->args[1]);
    }
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_minmaxnum(fir::Instr instr, fir::Function & /*funcy*/,
                        fir::FunctionR /*callee*/, bool is_min) {
    fir::Builder bb{instr};
    auto res = fir::ValueR{};
    // TODO: this has not the quite corret beheaviour in some edgecases
    res = bb.build_intrinsic(instr->args[1], instr->args[2],
                             is_min ? fir::IntrinsicSubType::FMin
                                    : fir::IntrinsicSubType::FMax);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_int_minmax(fir::Instr instr, fir::Function & /*funcy*/,
                         fir::FunctionR /*callee*/,
                         fir::IntrinsicSubType sub_type) {
    fir::Builder bb{instr};
    auto res = bb.build_intrinsic(instr->args[1], instr->args[2], sub_type);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_minmax(fir::Instr instr, fir::Function & /*funcy*/,
                        fir::FunctionR /*callee*/, bool is_min) {
    fir::Builder bb{instr};
    auto res = fir::ValueR{};
    // TODO: this has not the quite corret beheaviour in some edgecases
    res = bb.build_intrinsic(instr->args[1], instr->args[2],
                             is_min ? fir::IntrinsicSubType::FMinimum
                                    : fir::IntrinsicSubType::FMaximum);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_round(fir::Instr instr, fir::Function & /*funcy*/,
                    fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto res = fir::ValueR{};
    // TODO: NOT RIGHT SINCE ROUND I THINK GOES TORWARDS EVEN WHICH IT SHOULDNT
    res = bb.build_intrinsic(instr->args[1], fir::IntrinsicSubType::FRound);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_ceil(fir::Instr instr, fir::Function & /*funcy*/,
                   fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto res = fir::ValueR{};
    res = bb.build_intrinsic(instr->args[1], fir::IntrinsicSubType::FCeil);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_floor(fir::Instr instr, fir::Function & /*funcy*/,
                    fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto res = fir::ValueR{};
    res = bb.build_intrinsic(instr->args[1], fir::IntrinsicSubType::FFloor);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_trunc(fir::Instr instr, fir::Function & /*funcy*/,
                    fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto res = fir::ValueR{};
    res = bb.build_intrinsic(instr->args[1], fir::IntrinsicSubType::FTrunc);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_rint(fir::Instr instr, fir::Function & /*funcy*/,
                   fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto res = fir::ValueR{};
    // TODO: NOT EXACT
    res = bb.build_intrinsic(instr->args[1], fir::IntrinsicSubType::FRound);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_nearbyint(fir::Instr instr, fir::Function & /*funcy*/,
                        fir::FunctionR /*callee*/) {
    fir::Builder bb{instr};
    auto res = fir::ValueR{};
    // TODO: NOT EXACT
    res = bb.build_intrinsic(instr->args[1], fir::IntrinsicSubType::FRound);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_copysign(fir::Instr instr, fir::Function &funcy,
                       fir::FunctionR /*callee*/) {
    fir::Builder buh{instr};
    auto *ctx = funcy.ctx;
    auto ty = instr->get_type();
    auto mag_inp = instr->args[1];
    auto sign_inp = instr->args[2];

    fir::ValueR neg0Mask;
    if (ty->is_float() && ty->get_bitwidth() == 32) {
      neg0Mask =
          fir::ValueR{ctx->get_constant_value(-0.0F, ctx->get_float_type(32))};
    } else if (ty->is_float() && ty->get_bitwidth() == 64) {
      neg0Mask =
          fir::ValueR{ctx->get_constant_value(-0.0, ctx->get_float_type(64))};
    } else {
      fmt::println("impl copysing for {:cd}", instr);
    }
    auto sign =
        buh.build_binary_op(sign_inp, neg0Mask, fir::BinaryInstrSubType::And);
    auto mag = buh.build_binary_op(
        neg0Mask, buh.build_unary_op(mag_inp, fir::UnaryInstrSubType::Not),
        fir::BinaryInstrSubType::And);
    auto res = buh.build_binary_op(sign, mag, fir::BinaryInstrSubType::Or);

    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_cntpop(fir::Instr instr, fir::Function & /*funcy*/,
                     fir::FunctionR /*callee*/) {
    fir::Builder buh{instr};
    auto inp = instr->args[1];
    auto res = buh.build_intrinsic(inp, fir::IntrinsicSubType::PopCnt);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  void handle_bswap(fir::Instr instr, fir::Function & /*funcy*/,
                    fir::FunctionR /*callee*/) {
    fir::Builder buh{instr};
    auto res = buh.build_intrinsic(instr->args[1], fir::IntrinsicSubType::Bswap);
    instr->replace_all_uses(res);
    instr.destroy();
  }

  // a lowered call counts as modified
  bool modified = false;

  void apply(fir::BasicBlock bb, fir::Function &func) {
    // annoying copy
    //
    // TVec<fir::Instr> instrs = {bb->instructions.begin(),
    //                            bb->instructions.end()};
    for (size_t ip1 = bb->instructions.size(); ip1 > 0; ip1--) {
      auto i = ip1 - 1;
      auto instr = bb->instructions[i];
      if (!instr.is_valid()) {
        // instruction might have been deleted in a prior iteration
        continue;
      }
      if (instr->is(fir::InstrType::CallInstr)) {
        // const auto *callee =
        // instr->get_attrib("callee").try_string();
        if (!instr->args[0].is_constant()) {
          continue;
        }
        auto callee = instr->args[0].as_constant()->as_func();
        if (assume_cstdlib &&
            handle_libc_call(instr, func, callee)) {
          modified = true;
        } else if (callee.func->name.starts_with("llvm.memset.")) {
          handle_memset(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.memcpy.")) {
          handle_memcpy(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.fmuladd.")) {
          handle_fmuladd(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.trap")) {
          handle_trap(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.fabs")) {
          handle_fabs(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.abs")) {
          handle_abs(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.is.fpclass")) {
          handle_is_fpclass(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.expect")) {
          handle_expect(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.objectsize")) {
          handle_obj_size(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.va_start")) {
          handle_va(instr, func, callee, true);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.va_end")) {
          handle_va(instr, func, callee, false);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.umul.with.overflow")) {
          handle_umul_with_overflow(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.uadd.with.overflow")) {
          handle_uadd_with_overflow(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.lifetime.start") ||
                   callee.func->name.starts_with("llvm.lifetime.end")) {
          handle_lifetime(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.exp.f")) {
          handle_fexp(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.memmove")) {
          handle_memmove(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.ctlz")) {
          handle_ctlz(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.cttz")) {
          handle_cttz(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.is.constant")) {
          handle_is_constant(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.smin.")) {
          handle_int_minmax(instr, func, callee, fir::IntrinsicSubType::SMin);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.smax.")) {
          handle_int_minmax(instr, func, callee, fir::IntrinsicSubType::SMax);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.umin.")) {
          handle_int_minmax(instr, func, callee, fir::IntrinsicSubType::UMin);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.umax.")) {
          handle_int_minmax(instr, func, callee, fir::IntrinsicSubType::UMax);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.minnum")) {
          handle_minmaxnum(instr, func, callee, true);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.maxnum")) {
          handle_minmaxnum(instr, func, callee, false);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.maximum")) {
          handle_minmax(instr, func, callee, true);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.minimum")) {
          handle_minmax(instr, func, callee, false);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.trunc.")) {
          handle_trunc(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.ctpop.")) {
          handle_cntpop(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.round.")) {
          handle_round(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.bswap.") &&
                   instr->get_type()->is_int() &&
                   instr->get_type()->get_bitwidth() >= 16 &&
                   instr->get_type()->get_bitwidth() <= 64) {
          handle_bswap(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.ceil.")) {
          handle_ceil(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.floor.")) {
          handle_floor(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.rint.")) {
          handle_rint(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.nearbyint.")) {
          handle_nearbyint(instr, func, callee);
          modified = true;
        } else if (callee.func->name.starts_with("llvm.copysign.")) {
          handle_copysign(instr, func, callee);
          modified = true;
        }
      }
    }
  }

  PreservedAnalysis apply(fir::Context &ctx, fir::Function &func) override {
    ZoneScopedN("LLVMInstrinsLowering");
    assume_cstdlib = ctx.config->optim.assume_cstdlib_beheaviour;
    for (auto bb : func.basic_blocks) {
      apply(bb, func);
    }
    return modified ? PreservedAnalysis::none() : PreservedAnalysis::all();
  }
};
} // namespace foptim::optim
