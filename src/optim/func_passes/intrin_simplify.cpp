#include "intrin_simplify.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "optim/helper/helper.hpp"

namespace foptim::optim {

static void simplify_memcpy(fir::Instr instr, fir::BasicBlock bb,
                            fir::Context &ctx) {
  (void)instr;
  (void)bb;
  (void)ctx;

  if (!instr->args[3].is_constant()) {
    return;
  }
  auto size = instr->args[3].as_constant()->as_int();
  // TODO: support vector move
  if (size > 8) {
    return;
  }
  fmt::println("Simplify memcpy {:cd}", instr);
  auto dst_ptr = instr->args[1];
  auto src_ptr = instr->args[2];

  auto input_ty = guessType(src_ptr);
  auto output_ty = guessType(dst_ptr);
  fmt::println("==== {} {}   {} {}", input_ty.type, input_ty.typeless,
               output_ty.type, output_ty.typeless);
  // fmt::println("{}", instr->parent);
  // fmt::println("{} {}", input_ty.typeless, output_ty.typeless);
  if ((input_ty.typeless || output_ty.typeless) ||
      (input_ty.type.is_valid() && output_ty.type.is_valid() &&
       input_ty.type->eql(*output_ty.type.get_raw_ptr()))) {
    fir::Builder b{instr};

    auto selected_type =
        input_ty.type.is_valid()
            ? input_ty.type
            : (output_ty.type.is_valid() ? output_ty.type
                                         : ctx->get_int_type(size * 8));
    auto input = b.build_load(selected_type, src_ptr);
    b.build_store(dst_ptr, input);
    instr.destroy();
    // fmt::println("Simplify");
    return;
  }
}

static void simplify(fir::Instr instr, fir::BasicBlock bb, fir::Context &ctx) {
  if (!instr->is(fir::InstrType::CallInstr)) {
    return;
  }
  if (!instr->args[0].is_constant() ||
      !instr->args[0].as_constant()->is_func()) {
    return;
  }
  auto f = instr->args[0].as_constant()->as_func();
  if (instr->args.size() == 4 && f.func->name == "foptim.memcpy") {
    simplify_memcpy(instr, bb, ctx);
  }
}

void IntrinSimplify::apply(fir::Context &ctx, fir::Function &func) {
  using namespace foptim::fir;

  for (BasicBlock bb : func.basic_blocks) {
    auto &instrs = bb->get_instrs();
    for (size_t i = 0; i < instrs.size(); i++) {
      auto instr = instrs[i];
      simplify(instr, bb, ctx);
    }
  }
}

} // namespace foptim::optim
