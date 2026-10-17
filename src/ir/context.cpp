#include "context.hpp"

#include <atomic>

#include "global.hpp"
#include "ir/basic_block.hpp"
#include "ir/basic_block_arg.hpp"
#include "ir/constant_value.hpp"
#include "ir/types.hpp"
#include "utils/stable_vec_ref.hpp"
#include "utils/stable_vec_slot.hpp"
#include "utils/string.hpp"

namespace foptim::fir {

BasicBlock ContextData::copy(BasicBlock bb, V2VMap &subs, bool apply_subs) {
  auto res = storage.insert_bb(*bb.get_raw_ptr());
  subs.insert({ValueR{bb}, ValueR{res}});
  res->uses.clear();
  res->args.clear();
  for (u32 arg_id = 0; arg_id < bb->args.size(); arg_id++) {
    auto new_bb_arg = storage.insert_bb_arg(res, bb->args[arg_id]->get_type());
    subs.insert({ValueR{bb->args[arg_id]}, ValueR{new_bb_arg}});
    res.add_arg(new_bb_arg);
  }

  res->instructions.clear();
  for (auto &instr : bb->instructions) {
    auto new_instr = copy(instr);
    new_instr->uses.clear();
    res.push_instr(new_instr);
    new_instr->args.clear();
    for (size_t i = 0; i < instr->args.size(); i++) {
      new_instr.add_arg(instr->args[i]);
    }
    new_instr->bbs.clear();
    for (auto &bb : instr->bbs) {
      new_instr.add_bb(bb.bb);
      for (auto &arg : bb.args) {
        new_instr.add_bb_arg(new_instr->bbs.size() - 1, arg);
      }
    }
    if (apply_subs) {
      new_instr.substitute(subs);
    }
    map_results(instr, new_instr, subs);
  }
  return res;
}

BBArgument ContextData::copy(BBArgument bb_arg) {
  auto res = storage.insert_bb_arg(
      BBArgumentData{BasicBlock{BasicBlock::invalid()}, bb_arg->_type});
  res->uses.clear();
  return res;
}

Instr ContextData::copy(Instr instr) {
  auto res = storage.insert_instr(*instr.get_raw_ptr());
  return res;
}

void ContextData::print_stats() const {
  fmt::println("Funcs {}", storage.functions.size());
  fmt::println("Instr ");
  print_stats_vec(storage.storage_instr);
  fmt::println("BBs ");
  print_stats_vec(storage.basic_blocks);
  fmt::println("Constant ");
  print_stats_vec(storage.storage_constant);
  fmt::println("Type ");
  print_stats_vec(storage.storage_type);
  fmt::println("Global ");
  print_stats_vec(storage.storage_global);
}

bool ContextData::has_global(IRString name) const {
  auto *slot = storage.storage_global._slot_start.load();
  while (slot != nullptr) {
    for (auto &i : slot->data) {
      const auto *v = &i;
      if (v->is_used()) {
        if (v->data->name == name) {
          return true;
        }
      }
    }
    slot = slot->next;
  }
  return false;
}

Global ContextData::get_global(IRString name) {
  auto *slot = storage.storage_global._slot_start.load();
  while (slot != nullptr) {
    for (auto &i : slot->data) {
      auto *v = &i;
      if (v->is_used()) {
        if (v->data->name == name) {
#if SLOT_CHECK_LEVEL >= 2
          return fir::Global{utils::SRef{v, v->generation()}};
#else
          return fir::Global{utils::SRef{v, 0}};
#endif
        }
      }
    }
    slot = slot->next;
  }
  TODO("Tried to get global that doesnt exist");
}

Global ContextData::insert_global(IRString name, size_t size_bytes) {
  return storage.insert_global(std::make_unique<GlobalData>(name, size_bytes));
}

FunctionR ContextData::get_function(IRStringRef name) {
  auto *func = storage.functions.get(name);
  if (func == nullptr) {
    fmt::println("Failed to find function '{}' from storage", name);
    ASSERT(false);
  }
  return func;
}

bool ContextData::has_function(IRStringRef name) const {
  return storage.functions.contains(name);
}

FunctionR ContextData::create_function(IRString name, FunctionTypeR type) {
  auto func = FunctionR(storage.functions.insert(
      name, std::make_unique<Function>(this, name, type)));
  auto init_bb = BasicBlock(storage.basic_blocks.push_back({func}));
  init_bb.verify_validness();
  func->append_bbr(init_bb);
  // func->set_entry_bbr(init_bb);
  const auto &arg_tys = type->as_func().arg_types;
  init_bb->args.reserve(arg_tys.size());
  for (auto arg_ty : arg_tys) {
    init_bb->args.push_back(storage.insert_bb_arg(init_bb, arg_ty));
  }
  return func;
}

bool ContextData::delete_function(IRStringRef delete_func) {
  return storage.functions.erase(delete_func);
}

bool ContextData::verify() const {
  for (const auto *func : storage.functions.all()) {
    if (!func->verify()) {
      fmt::println("In Function: {}\n", func->name.c_str());
      return false;
    }
  }
  return true;
}

VoidTypeR ContextData::get_void_type() {
  static auto void_type = storage.insert_type(AnyType{});
  return void_type;
}

TypeR ContextData::get_ptr_type() {
  static auto ptr_type = storage.insert_type(AnyType::Ptr());
  return ptr_type;
}

TypeR ContextData::get_vec_type(fir::TypeR elem_ty, u16 n_lanes) {
  VectorType::SubType elemy = VectorType::SubType::Integer;
  u16 width = 0;
  if (elem_ty->is_int()) {
    elemy = VectorType::SubType::Integer;
    width = elem_ty->as_int();
  } else if (elem_ty->is_float()) {
    elemy = VectorType::SubType::Floating;
    width = elem_ty->as_float();
  } else if (elem_ty->is_ptr()) {
    elemy = VectorType::SubType::Integer;
    width = 64;
  } else {
    fmt::println("{}", elem_ty);
    TODO("INVALID?");
  }

  auto ty = VectorType(elemy, width, n_lanes);
  return intern_type(ty);
}

TypeR ContextData::get_vec_type(fir::VectorType::SubType elem_ty, u16 width,
                                u16 n_lanes) {
  auto ty = VectorType(elem_ty, width, n_lanes);
  return intern_type(ty);
}

IntTypeR ContextData::get_float_type(u16 bitwidth) {
  static auto f32_type = storage.insert_type(FloatType{32});
  static auto f64_type = storage.insert_type(FloatType{64});
  if (bitwidth == 32) {
    return f32_type;
  }
  if (bitwidth == 64) {
    return f64_type;
  }
  return intern_type(FloatType(bitwidth));
}

IntTypeR ContextData::get_int_type(u16 bitwidth) {
  static auto u1_type = storage.insert_type(IntegerType{1});
  static auto u8_type = storage.insert_type(IntegerType{8});
  static auto u16_type = storage.insert_type(IntegerType{16});
  static auto u32_type = storage.insert_type(IntegerType{32});
  static auto u64_type = storage.insert_type(IntegerType{64});
  if (bitwidth == 1) {
    return u1_type;
  }
  if (bitwidth == 8) {
    return u8_type;
  }
  if (bitwidth == 16) {
    return u16_type;
  }
  if (bitwidth == 32) {
    return u32_type;
  }
  if (bitwidth == 64) {
    return u64_type;
  }
  return intern_type(IntegerType(bitwidth));
}

FunctionTypeR ContextData::get_func_ty(TypeR ret_type, IRVec<TypeR> args) {
  return get_func_ty(std::span<const TypeR>{&ret_type, 1}, std::move(args));
}

FunctionTypeR ContextData::get_func_ty(std::span<const TypeR> ret_types,
                                       IRVec<TypeR> args) {
  ASSERT(!ret_types.empty());
  ASSERT_M(
      ret_types.size() == 1 ||
          std::ranges::none_of(ret_types, [](TypeR t) { return t->is_void(); }),
      "void can only be the single result type");
  auto func_ty =
      FunctionType{.return_types = {ret_types.begin(), ret_types.end()},
                   .arg_types = std::move(args)};
  return intern_type(func_ty);
}

ConstantValueR ContextData::get_poisson_value(TypeR type) {
  auto ty = ConstantValue(type);
  return intern_constant(ty);
}

ConstantValueR ContextData::intern_constant(const ConstantValue &val) {
  return ConstantValueR(constant_table.intern(
      val.hash(),
      [&](const utils::SRef<ConstantValue> &r) {
        return r.is_valid() && r.data_ref->data.ty == val.ty &&
               r.data_ref->data.get_type() == val.get_type() &&
               r.data_ref->data.eql(val);
      },
      [&] { return utils::SRef<ConstantValue>(storage.insert_constant(val)); }));
}

TypeR ContextData::intern_type(const AnyType &val) {
  return TypeR(type_table.intern(
      val.hash(),
      [&](const utils::SRef<AnyType> &r) {
        return r.is_valid() && r.data_ref->data.eql(val);
      },
      [&] { return utils::SRef<AnyType>(storage.insert_type(val)); }));
}

ConstantValueR ContextData::get_constant_value(FunctionR func) {
  const auto constant = ConstantValue(func, get_ptr_type());
  return intern_constant(constant);
}

ConstantValueR ContextData::get_constant_value(ConstantValue constant) {
  return intern_constant(constant);
}

ConstantValueR ContextData::get_constant_value(f32 val, FloatTypeR ty) {
  const auto constant = ConstantValue(val, ty);
  return intern_constant(constant);
}

ConstantValueR ContextData::get_constant_value(f64 val, FloatTypeR ty) {
  const auto constant = ConstantValue(val, ty);
  return intern_constant(constant);
}

ConstantValueR ContextData::get_constant_value(u64 val, IntTypeR ty) {
  const auto constant = ConstantValue(val, ty);
  return intern_constant(constant);
}

ConstantValueR ContextData::get_constant_value(i64 val, IntTypeR ty) {
  const auto constant = ConstantValue(val, ty);
  return intern_constant(constant);
}

ConstantValueR ContextData::get_constant_value(i32 val, IntTypeR ty) {
  const auto constant = ConstantValue(static_cast<i64>(val), ty);
  return intern_constant(constant);
}

ConstantValueR ContextData::get_constant_null() {
  static const auto null_const =
      storage.insert_constant(ConstantValue::null_ptr(get_ptr_type()));
  return null_const;
}

ConstantValueR ContextData::get_constant_value(i128 val, IntTypeR ty) {
  const auto constant = ConstantValue(val, ty);
  return intern_constant(constant);
}

ConstantValueR ContextData::get_constant_int(i128 val, u32 bitwidth) {
  const auto constant = ConstantValue(val, get_int_type(bitwidth));
  return intern_constant(constant);
}

ConstantValueR ContextData::get_constant_int(APInt val) {
  const auto ty = get_int_type(val.bits());
  const auto constant = ConstantValue(std::move(val), ty);
  return intern_constant(constant);
}

ConstantValueR ContextData::get_constant_value(IRVec<ConstantValueR> data,
                                               TypeR vec_ty) {
  const auto constant = ConstantValue(std::move(data), vec_ty);
  return intern_constant(constant);
}

ConstantValueR ContextData::get_constant_value(u32 val, IntTypeR ty) {
  const auto constant = ConstantValue(static_cast<u64>(val), ty);
  return intern_constant(constant);
}

ConstantValueR ContextData::get_constant_value(Global glob) {
  static auto global_ptr_typee = get_ptr_type();

  const auto constant = ConstantValue(glob, global_ptr_typee);
  return intern_constant(constant);
}
void ContextData::dump_graph(const char *filename) {
  // digraph G {
  //  compound=true;
  //  rankdir=TB;
  //  node [shape=box, fontname="monospace", fontsize=10];
  //  // Function 1 as a cluster
  //  subgraph cluster_func1 {
  //      label = "Function: 0x55f551a2e420";
  //      color = lightgrey;

  //      BB1 [label="BB_1:\n  inst1\n  inst2"];
  //      BB2 [label="BB_2:\n  inst3"];
  //      BB1 -> BB2;
  //  }
  //  // Function 2 as another cluster
  //  subgraph cluster_func2 {
  //      label = "Function: 0x55f551a94e20";
  //      color = lightblue;
  //      BB3 [label="BB_3:\n  inst4"];
  //      BB4 [label="BB_4:\n  inst5"];
  //      BB3 -> BB4;
  //  }
  //  // Cross-function edges
  //  BB2 -> BB3

  auto *file = std::fopen(filename, "w");
  if (file == nullptr) {
    TODO("failed to open output dump file");
  }
  fmt::print(file, "digraph G{{\ncompound = true;\nrankdir=TB;\n");
  fmt::print(file, "node [shape=box, fontname=\"monospace\", fontsize=10];\n");
  for (const auto *f : storage.functions.all()) {
    fmt::print(
        file,
        "subgraph \"cluster_{}\" {{\n label = \"Function: {}\";\n color = "
        "lightgrey;\n",
        f->name, f->name);
    for (auto bb : f->basic_blocks) {
      fmt::print(file, R"( "{}"[label="{}:\n)",
                 static_cast<const void *>(bb.get_raw_ptr()),
                 static_cast<const void *>(bb.get_raw_ptr()));
      for (auto i : bb->instructions) {
        fmt::print(file, R"(    {}\n)", i);
      }
      fmt::print(file, "\"];\n");
      u32 i = 0;
      constexpr u32 N_EDGE_COLORS = 6;
      constexpr const char *edgecolors[N_EDGE_COLORS] = {
          "green", "red", "blue", "purple", "yellow", "black"};
      for (auto &t : bb->get_terminator()->bbs) {
        fmt::print(file, "\"{}\" -> \"{}\" [color=\"{}\"];\n",
                   static_cast<const void *>(bb.get_raw_ptr()),
                   static_cast<const void *>(t.bb.get_raw_ptr()),
                   edgecolors[std::min(i, N_EDGE_COLORS - 1)]);
        i++;
      }
    }
    fmt::print(file, "}}\n");
  }
  // fmt::print(file,
  //            "subgraph cluster_func1 {{label = \"Function: 0x55f551a2e420\";
  //            " "color = lightgrey;BB1 [label=\"BB_1:\n  inst1\n  inst2\"];BB2
  //            "
  //            "[label=\"BB_2:\n  inst3\"];BB1 -> BB2;}}\n");
  // Function 2 as another cluster
  // fmt::print(file, "subgraph cluster_func2 {{"
  //                  "label = \"Function: 0x55f551a94e20\";"
  //                  "color = lightblue;"
  //                  "BB3[label = \"BB_3:\n  inst4\"];"
  //                  "BB4[label = \"BB_4:\n  inst5\"];"
  //                  "BB3->BB4;}}\n");
  // fmt::print(
  //     file,
  //     "BB2->BB3 [ltail=cluster_func1 lhead=cluster_func2
  //     label=\"call\"];\n");
  fmt::print(file, "}}\n");
  fclose(file);
}

}  // namespace foptim::fir

fmt::appender fmt::formatter<foptim::fir::Context>::format(
    foptim::fir::Context const &v, format_context &ctx) const {
  auto app = ctx.out();
  auto *slot = v->storage.storage_global._slot_start.load();
  while (slot != nullptr) {
    for (auto &i : slot->data) {
      const auto *glob = &i;
      if (glob->is_used()) {
        if (color) {
          app = fmt::format_to(app, "{:cd}\n", *glob->data);
        } else {
          app = fmt::format_to(app, "{:d}\n", *glob->data);
        }
      }
    }
    slot = slot->next;
  }
  for (const auto *func : v.data->storage.functions.all()) {
    if (color) {
      app = fmt::format_to(app, "{:cd}\n", *func);
    } else {
      app = fmt::format_to(app, "{:d}\n", *func);
    }
  }
  return app;
}
