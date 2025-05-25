#include "intrin_simplify.hpp"
#include "ir/builder.hpp"
#include "ir/instruction_data.hpp"

namespace foptim::optim {

struct GuessTypeResult {
  bool typeless;
  fir::TypeR type;
};

static GuessTypeResult guessType(fir::ValueR ptr) {
  if (ptr.is_constant()) {
    return {true, fir::TypeR{fir::TypeR::invalid()}};
  }
  if (ptr.is_bb_arg()) {
    // auto bb_arg = ptr.as_bb_arg();
    // if (bb_arg->_parent == bb_arg->_parent->get_parent()->get_entry()) {
    return {true, fir::TypeR{fir::TypeR::invalid()}};
    // }
  }
  if (ptr.is_instr()) {
    auto ptr_instr = ptr.as_instr();
    if (ptr_instr->is(fir::InstrType::AllocaInstr)) {
      if (ptr_instr->has_attrib("alloca::type")) {
        return {false, *ptr_instr->get_attrib("alloca::type").try_type()};
      }
      return {true, fir::TypeR{fir::TypeR::invalid()}};
    }
    if (ptr_instr->is(fir::InstrType::LoadInstr)) {
      return {true, fir::TypeR{fir::TypeR::invalid()}};
    }
    if (ptr_instr->is(fir::InstrType::BinaryInstr) &&
        (fir::BinaryInstrSubType)ptr_instr->subtype ==
            fir::BinaryInstrSubType::IntAdd) {
      GuessTypeResult out_res = guessType(ptr_instr->args[0]);
      GuessTypeResult r2 = guessType(ptr_instr->args[1]);
      if (out_res.typeless && !r2.typeless) {
        return r2;
      }
      if (!out_res.typeless && !r2.typeless) {
        return {false, fir::TypeR{fir::TypeR::invalid()}};
      }
      return out_res;
    }
  }
  return {false, fir::TypeR{fir::TypeR::invalid()}};
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
  // TODO: support vector move
  if (size > 8) {
    return;
  }
  auto dst_ptr = instr->args[1];
  auto src_ptr = instr->args[2];

  auto input_ty = guessType(src_ptr);
  auto output_ty = guessType(dst_ptr);
  // fmt::println("{}", instr->parent);
  // fmt::println("{} {}", input_ty.typeless, output_ty.typeless);
  if ((input_ty.typeless && output_ty.typeless) ||
      (input_ty.type.is_valid() && output_ty.type.is_valid() &&
       input_ty.type->eql(*output_ty.type.get_raw_ptr()))) {
    fir::Builder b{instr};
    auto input = b.build_load(
        input_ty.type.is_valid() ? input_ty.type : ctx->get_int_type(size * 8),
        src_ptr);
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
