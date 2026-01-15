#include "helpers.hpp"
#include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/global.hpp"

namespace foptim::fir {
void generate_fexp(foptim::fir::Context &fctx) {
  if (fctx->has_function("exp")) {
    return;
  }
  auto func_ty =
      fctx->get_func_ty(fctx->get_float_type(64), {fctx->get_float_type(64)});
  fctx.data->storage.functions.insert(
      {"exp", std::make_unique<foptim::fir::Function>(fctx.operator->(), "exp",
                                                      func_ty)});
}

void generate_memmove(foptim::fir::Context &fctx) {
  if (fctx->has_function("memmove")) {
    return;
  }
  auto func_ty = fctx->get_func_ty(
      fctx->get_ptr_type(),
      {fctx->get_ptr_type(), fctx->get_ptr_type(), fctx->get_int_type(64)});
  fctx.data->storage.functions.insert(
      {"memmove", std::make_unique<foptim::fir::Function>(fctx.operator->(),
                                                          "memmove", func_ty)});
}

void generate_memset(foptim::fir::Context &fctx) {
  const auto *name = "foptim.memset";
  if (fctx->has_function(name)) {
    return;
  }
  auto func_ty = fctx->get_func_ty(
      fctx->get_void_type(),
      {fctx->get_ptr_type(), fctx->get_int_type(8), fctx->get_int_type(64)});
  auto ffunc = fctx->create_function(name, func_ty);
  ffunc.func->linkage = foptim::fir::Linkage::LinkOnceODR;

  auto bb = ffunc.builder();
  auto entry_bb = ffunc->get_entry();
  auto loop_body = bb.append_bb();
  auto exit = bb.append_bb();

  // the arguments
  auto ptr_arg = foptim::fir::ValueR{entry_bb->args[0]};
  auto value_arg = foptim::fir::ValueR{entry_bb->args[1]};
  auto length_arg = foptim::fir::ValueR{entry_bb->args[2]};

  auto i64_ty = fctx->get_int_type(64);
  auto constant_zero = foptim::fir::ValueR(fctx->get_constant_value(0, i64_ty));
  auto constant_one = foptim::fir::ValueR(fctx->get_constant_value(1, i64_ty));
  auto constant_eight =
      foptim::fir::ValueR(fctx->get_constant_value(8, i64_ty));

  // === header
  bb.at_end(ffunc->get_entry());
  // i = 0
  auto index = bb.build_alloca(constant_eight);
  index.as_instr()->extra_type = i64_ty;
  bb.build_store(index, constant_zero);
  // if(length != 0)
  auto loop_cond = bb.build_int_cmp(length_arg, constant_zero,
                                    foptim::fir::ICmpInstrSubType::NE);
  bb.build_cond_branch(loop_cond, loop_body, exit);

  // === loop
  {
    bb.at_end(loop_body);
    auto old_index_val = bb.build_load(i64_ty, index);
    // ptr+i = value
    auto target_offset = bb.build_int_add(ptr_arg, old_index_val);
    bb.build_store(target_offset, value_arg);

    // i++
    auto new_index_val = bb.build_int_add(old_index_val, constant_one);
    bb.build_store(index, new_index_val);
    // while(i+1 < length)
    auto loop_cond = bb.build_int_cmp(new_index_val, length_arg,
                                      foptim::fir::ICmpInstrSubType::ULT);
    bb.build_cond_branch(loop_cond, loop_body, exit);
  }

  // return
  bb.at_end(exit);
  bb.build_return();
}

void generate_memcpy(foptim::fir::Context &fctx) {
  const auto *name = "foptim.memcpy";
  if (fctx->has_function(name)) {
    return;
  }
  auto func_ty = fctx->get_func_ty(
      fctx->get_void_type(),
      {fctx->get_ptr_type(), fctx->get_ptr_type(), fctx->get_int_type(64)});
  auto ffunc = fctx->create_function(name, func_ty);
  ffunc.func->linkage = foptim::fir::Linkage::Internal;

  auto bb = ffunc.builder();
  auto entry_bb = ffunc->get_entry();
  auto loop_body = bb.append_bb();
  auto exit = bb.append_bb();

  // the arguments
  entry_bb->args[0]->noalias = true;
  auto dst_ptr_arg = foptim::fir::ValueR{entry_bb->args[0]};
  entry_bb->args[1]->noalias = true;
  auto src_ptr_arg = foptim::fir::ValueR{entry_bb->args[1]};
  auto length_arg = foptim::fir::ValueR{entry_bb->args[2]};

  auto i8_ty = fctx->get_int_type(8);
  auto i64_ty = fctx->get_int_type(64);
  auto constant_zero = foptim::fir::ValueR(fctx->get_constant_value(0, i64_ty));
  auto constant_one = foptim::fir::ValueR(fctx->get_constant_value(1, i64_ty));
  auto constant_eight =
      foptim::fir::ValueR(fctx->get_constant_value(8, i64_ty));

  // === header
  bb.at_end(ffunc->get_entry());
  // i = 0
  auto index = bb.build_alloca(constant_eight);
  index.as_instr()->extra_type = i64_ty;
  bb.build_store(index, constant_zero);
  // if(length != 0)
  auto loop_cond = bb.build_int_cmp(length_arg, constant_zero,
                                    foptim::fir::ICmpInstrSubType::NE);
  bb.build_cond_branch(loop_cond, loop_body, exit);

  // === loop
  {
    bb.at_end(loop_body);
    auto old_index_val = bb.build_load(i64_ty, index);
    // ptr+i = value
    auto src_offset = bb.build_int_add(src_ptr_arg, old_index_val, true, true);
    auto val = bb.build_load(i8_ty, src_offset);
    auto dst_offset = bb.build_int_add(dst_ptr_arg, old_index_val, true, true);
    bb.build_store(dst_offset, val);

    // i++
    auto new_index_val =
        bb.build_int_add(old_index_val, constant_one, true, true);
    bb.build_store(index, new_index_val);

    // while(i+1 < length)
    auto loop_cond = bb.build_int_cmp(new_index_val, length_arg,
                                      foptim::fir::ICmpInstrSubType::ULT);
    bb.build_cond_branch(loop_cond, loop_body, exit);
  }

  // return
  bb.at_end(exit);
  bb.build_return();
}

void generate_trap(foptim::fir::Context &fctx) {
  if (fctx->has_function("abort")) {
    return;
  }
  auto func_ty = fctx->get_func_ty(fctx->get_void_type(), {});
  fctx.data->storage.functions.insert(
      {"abort", std::make_unique<foptim::fir::Function>(fctx.operator->(),
                                                        "abort", func_ty)});
}

BasicBlock insert_bb_between(BasicBlock from, BasicBlock to) {
  bool found = false;
  for (const auto &f : from->get_terminator()->bbs) {
    if (f.bb == to) {
      found = true;
      break;
    }
  }
  if (!found) {
    fmt::println("{} {}", from, to);
    ASSERT_M(false,
             "Tried to insert bb between 2 blocks but they arent neighbours");
  }
  auto bb_term = from->get_terminator();
  auto bb_indx = bb_term.get_bb_id(to);
  const auto &bb_args = bb_term->bbs[bb_indx].args;

  fir::Builder bb{from};
  auto edge_bb = bb.append_bb();
  bb.at_end(edge_bb);

  auto edge_term = bb.build_branch(to);
  for (auto old_arg : bb_args) {
    edge_term.add_bb_arg(0, old_arg);
  }

  bb_term.replace_bb(bb_indx, edge_bb, true);
  return edge_bb;
}

void convert_constant_init(u8 *output, fir::ConstantValueR val, Global glob) {
  (void)output;
  switch (val->ty) {
    case ConstantType::PoisonValue:
      return;
    case ConstantType::FloatValue:
      switch (val->type->as_float()) {
        case 32:
          *((f32 *)output) = val->as_f32();
          return;
        case 64:
          *((f64 *)output) = val->as_f64();
          return;
        default:
          fmt::println("{}", val);
          TODO("okakf");
      }
      break;
    case ConstantType::NullPtr:
      *((u64 *)output) = 0;
      return;
    case ConstantType::IntValue:
      switch (val->type->get_bitwidth()) {
        case 8:
          *output = (u8)val->int_u.v.data;
          return;
        case 16:
          *((u16 *)output) = (u16)val->int_u.v.data;
          return;
        case 32:
          *((u32 *)output) = (u32)val->int_u.v.data;
          return;
        case 64:
          *((u64 *)output) = (u64)val->int_u.v.data;
          return;
        default:
          fmt::println("{}", val);
          TODO("okaka");
      }
      break;
    case ConstantType::VectorValue: {
      auto typee = val->type->as_vec();
      auto width = typee.bitwidth;
      size_t i = 0;
      for (auto m : val->vec_u.v.members) {
        convert_constant_init(output + (((width + 7) / 8) * i), m, glob);
        i++;
      }
      return;
    }
    case ConstantType::GlobalPtr: {
      glob->reloc_info.push_back(GlobalData::RelocationInfo{
          .insert_offset = (size_t)output - (size_t)glob->init_value,
          .ref = val,
          .reloc_offset = 0});
      return;
    }
    case ConstantType::FuncPtr: {
      glob->reloc_info.push_back(GlobalData::RelocationInfo{
          .insert_offset = (size_t)output - (size_t)glob->init_value,
          .ref = val,
          .reloc_offset = 0});
      return;
      return;
    }
    case ConstantType::ConstantStruct:
      break;
  }
  fmt::println("{}", val);
  TODO("okaku");
}

}  // namespace foptim::fir
