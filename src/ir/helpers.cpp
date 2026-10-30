#include "helpers.hpp"

#include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/global.hpp"

namespace foptim::fir {
void generate_trunc(foptim::fir::Context &fctx) {
  if (!fctx->has_function("trunc")) {
    auto func_ty =
        fctx->get_func_ty(fctx->get_float_type(64), {fctx->get_float_type(64)});
    fctx.data->storage.functions.insert(
        "trunc", std::make_unique<foptim::fir::Function>(fctx.operator->(),
                                                         "trunc", func_ty));
  }
  if (!fctx->has_function("truncf")) {
    auto func_ty =
        fctx->get_func_ty(fctx->get_float_type(32), {fctx->get_float_type(32)});
    fctx.data->storage.functions.insert(
        "truncf", std::make_unique<foptim::fir::Function>(fctx.operator->(),
                                                          "truncf", func_ty));
  }
}

// Lowering of the memmove / memcmp intrinsics calls these late, in a function
// pass, where no function can be added anymore. So the declarations stay in the
// module even when nothing calls them yet, which needs external linkage
// (GDCE keeps those).
static void declare_external(foptim::fir::Context &fctx, LibFunc lf,
                             foptim::fir::FunctionTypeR func_ty,
                             const char *name = nullptr) {
  if (name == nullptr) {
    name = libfunc_name(lf).data();
  }
  if (fctx->has_function(name)) {
    return;
  }
  auto *f = fctx.data->storage.functions.insert(
      name, std::make_unique<foptim::fir::Function>(fctx.operator->(), name,
                                                    func_ty));
  f->attribs.linkage = Linkage::External;
  f->attribs.libfunc = lf;
}

void generate_memcpy(foptim::fir::Context &fctx) {
  declare_external(
      fctx, LibFunc::Memcpy,
      fctx->get_func_ty(fctx->get_ptr_type(),
                        {fctx->get_ptr_type(), fctx->get_ptr_type(),
                         fctx->get_int_type(64)}));
}

void generate_memset(foptim::fir::Context &fctx) {
  declare_external(
      fctx, LibFunc::Memset,
      fctx->get_func_ty(fctx->get_ptr_type(),
                        {fctx->get_ptr_type(), fctx->get_int_type(32),
                         fctx->get_int_type(64)}));
}

void generate_memmove(foptim::fir::Context &fctx) {
  declare_external(
      fctx, LibFunc::Memmove,
      fctx->get_func_ty(fctx->get_ptr_type(),
                        {fctx->get_ptr_type(), fctx->get_ptr_type(),
                         fctx->get_int_type(64)}));
}

void generate_memcmp(foptim::fir::Context &fctx) {
  declare_external(
      fctx, LibFunc::Memcmp,
      fctx->get_func_ty(fctx->get_int_type(32),
                        {fctx->get_ptr_type(), fctx->get_ptr_type(),
                         fctx->get_int_type(64)}));
}

void generate_fmod(foptim::fir::Context &fctx) {
  auto f64 = fctx->get_float_type(64);
  auto f32 = fctx->get_float_type(32);
  declare_external(fctx, LibFunc::Fmod, fctx->get_func_ty(f64, {f64, f64}));
  declare_external(fctx, LibFunc::Fmod, fctx->get_func_ty(f32, {f32, f32}),
                   "fmodf");
}

void generate_libm_for_intrinsic(foptim::fir::Context &fctx,
                                 std::string_view llvm_name) {
  const auto f32_name = intrinsic_libm_name(llvm_name);
  if (f32_name.empty()) {
    return;
  }
  auto f64 = fctx->get_float_type(64);
  auto f32 = fctx->get_float_type(32);
  const std::string base = llvm_name.ends_with(".f32")
                               ? f32_name.substr(0, f32_name.size() - 1)
                               : f32_name;
  const auto info = lookup_libfunc(base);
  ASSERT(info.has_value());
  const bool two = info->n_args == 2;
  declare_external(fctx, info->func,
                   fctx->get_func_ty(f64, two ? std::initializer_list{f64, f64}
                                              : std::initializer_list{f64}),
                   base.c_str());
  const auto fname = base + "f";
  declare_external(fctx, info->func,
                   fctx->get_func_ty(f32, two ? std::initializer_list{f32, f32}
                                              : std::initializer_list{f32}),
                   fname.c_str());
}

void generate_trap(foptim::fir::Context &fctx) {
  if (fctx->has_function("abort")) {
    return;
  }
  auto func_ty = fctx->get_func_ty(fctx->get_void_type(), {});
  fctx.data->storage.functions.insert(
      "abort", std::make_unique<foptim::fir::Function>(fctx.operator->(),
                                                       "abort", func_ty));
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

  // the arguments now belong to the branch of the new block
  bb_term.replace_bb(bb_indx, edge_bb, false);
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
          *(reinterpret_cast<f32 *>(output)) = val->as_f32();
          return;
        case 64:
          *(reinterpret_cast<f64 *>(output)) = val->as_f64();
          return;
        default:
          fmt::println("{}", val);
          TODO("okakf");
      }
      break;
    case ConstantType::NullPtr:
      *(reinterpret_cast<u64 *>(output)) = 0;
      return;
    case ConstantType::IntValue:
      switch (val->type->get_bitwidth()) {
        case 8:
          *output = static_cast<u8>(val->int_u.v.data.to_u64());
          return;
        case 16:
          *(reinterpret_cast<u16 *>(output)) =
              static_cast<u16>(val->int_u.v.data.to_u64());
          return;
        case 32:
          *(reinterpret_cast<u32 *>(output)) =
              static_cast<u32>(val->int_u.v.data.to_u64());
          return;
        case 64:
          *(reinterpret_cast<u64 *>(output)) =
              static_cast<u64>(val->int_u.v.data.to_u64());
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
      ASSERT(width % 8 == 0);
      auto width_byte = (width / 8);
      for (auto m : val->vec_u.v.members) {
        convert_constant_init(output + (width_byte * i), m, glob);
        i++;
      }
      return;
    }
    case ConstantType::GlobalPtr: {
      glob->reloc_info.push_back(GlobalData::RelocationInfo{
          .insert_offset = std::bit_cast<size_t>(output) -
                           std::bit_cast<size_t>(glob->init_value),
          .ref = val,
          .reloc_offset = 0});
      return;
    }
    case ConstantType::FuncPtr: {
      glob->reloc_info.push_back(GlobalData::RelocationInfo{
          .insert_offset = std::bit_cast<size_t>(output) -
                           std::bit_cast<size_t>(glob->init_value),
          .ref = val,
          .reloc_offset = 0});
      return;
      return;
    }
  }
  fmt::println("{}", val);
  TODO("okaku");
}

}  // namespace foptim::fir
