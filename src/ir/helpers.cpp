#include "helpers.hpp"
#include "ir/basic_block.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/global.hpp"

namespace foptim::fir {
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
      switch (val->type->as_int()) {
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
