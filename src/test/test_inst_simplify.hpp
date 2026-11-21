#include <gtest/gtest.h>

#include <array>

#include "ir/builder.hpp"
#include "ir/context.hpp"
#include "ir/value.hpp"
#include "optim/func_passes/inst_simplify.hpp"

namespace {
using foptim::fir::ValueR;

// f(a: 4xf32, b: 4xf32) with the return built by the callback from concat(a, b)
template <class MkRet>
foptim::fir::FunctionR build_concat_func(foptim::fir::Context &ctx,
                                         foptim::fir::TypeR ret_ty,
                                         MkRet mk_ret) {
  auto f32 = ctx.data->get_float_type(32);
  auto v4f = ctx.data->get_vec_type(f32, 4);
  auto v8f = ctx.data->get_vec_type(f32, 8);
  auto func =
      ctx.data->create_function("f", ctx.data->get_func_ty(ret_ty, {v4f, v4f}));
  auto b = func.builder();
  auto entry = func.func->get_entry();
  b.at_end(entry);
  auto cc = b.build_vector_op(ValueR{entry->args[0]}, ValueR{entry->args[1]},
                              v8f, foptim::fir::VectorISubType::Concat);
  b.build_return(mk_ret(b, cc));
  return func;
}
}  // namespace

TEST(InstSimplify, ExtractOfWideConcat) {
  for (u32 idx = 0; idx < 8; idx++) {
    foptim::fir::Context ctx{nullptr};
    auto f32 = ctx.data->get_float_type(32);
    auto i32 = ctx.data->get_int_type(32);
    auto func =
        build_concat_func(ctx, f32, [&](foptim::fir::Builder &b, ValueR cc) {
          std::array<ValueR, 1> index{
              ValueR{ctx.data->get_constant_value(idx, i32)}};
          return b.build_extract_element(cc, index, f32);
        });
    foptim::optim::InstSimplify{}.apply(ctx, *func.func);
    auto entry = func.func->get_entry();
    auto ret = entry->instructions.back();
    ASSERT_TRUE(ret->args[0].is_instr());
    auto ext = ret->args[0].as_instr();
    ASSERT_TRUE(ext->is(foptim::fir::InstrType::ExtractElement));
    // lane idx of the concat is lane idx % 4 of the half it came from
    EXPECT_EQ(ext->args[0], ValueR{entry->args[idx / 4]});
    EXPECT_EQ(ext->args[1].as_constant()->as_int(), idx % 4);
  }
}

TEST(InstSimplify, ExtractLowOfConcatKeepsType) {
  foptim::fir::Context ctx{nullptr};
  auto v4i = ctx.data->get_vec_type(ctx.data->get_int_type(32), 4);
  auto func =
      build_concat_func(ctx, v4i, [&](foptim::fir::Builder &b, ValueR cc) {
        return b.build_vector_op(cc, v4i,
                                 foptim::fir::VectorISubType::ExtractLow);
      });
  foptim::optim::InstSimplify{}.apply(ctx, *func.func);
  auto ret = func.func->get_entry()->instructions.back();
  EXPECT_EQ(ret->args[0].get_type(), v4i);
}
