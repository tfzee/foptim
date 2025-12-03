#include "intrin_simplify.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"
#include "optim/analysis/access_analysis.hpp"
#include "optim/helper/helper.hpp"

namespace foptim::optim {

/*If we memcpy into a local alloca and the len of the memcpy is equal to alloca
length and afterwards we only read from it*/
static bool delete_useless_memcpy(fir::Instr instr) {
  auto target_ptr = instr->args[1];
  auto copy_len = instr->args[3];
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
  if (alloca_len.as_constant()->as_int() != copy_len.as_constant()->as_int()) {
    return false;
  }
  for (auto u : target_ptr_instr->get_uses()) {
    if (u.user == instr && u.argId == 1) {
      continue;
    }
    AccessResult res;
    useptr_access_analysis(u, res);
    if (res.IsWriten || res.VolatileRead || res.VolatileWrite || res.Escapes) {
      return false;
    }
  }
  target_ptr_instr->replace_all_uses(instr->args[2]);
  instr.destroy();
  return true;
}

static void simplify_memcpy(fir::Instr instr, fir::BasicBlock bb,
                            fir::Context &ctx) {
  (void)instr;
  (void)bb;
  (void)ctx;

  if (!instr->args[3].is_constant()) {
    return;
  }
  auto size = instr->args[3].as_constant()->as_int();

  // delete uselless memcpy
  if (delete_useless_memcpy(instr)) {
    return;
  }

  if (size > 8 && size != 16 && size != 32) {
    return;
  }
  auto dst_ptr = instr->args[1];
  auto src_ptr = instr->args[2];

  auto input_ty = guessType(src_ptr);
  auto output_ty = guessType(dst_ptr);
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
    auto input = b.build_load(selected_type, src_ptr);
    b.build_store(dst_ptr, input);
    instr.destroy();
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
  ZoneScopedNC("IntrinSimplify", COLOR_OPTIMF);

  for (BasicBlock bb : func.basic_blocks) {
    auto &instrs = bb->get_instrs();
    for (size_t i = 0; i < instrs.size(); i++) {
      auto instr = instrs[i];
      simplify(instr, bb, ctx);
    }
  }
}

}  // namespace foptim::optim
