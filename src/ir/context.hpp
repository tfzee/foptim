#pragma once
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/value.hpp"
#include "storage.hpp"
#include "types.hpp"
#include "types_ref.hpp"
#include "utils/logging.hpp"
#include "utils/stable_vec.hpp"
#include <fmt/format.h>

namespace foptim::fir {

struct ContextData {
  IRStorage storage = {};
  using V2VMap = TMap<ValueR, ValueR>;

  BasicBlock copy(BasicBlock bb, V2VMap &subs, bool apply_subs = true);
  BBArgument copy(BBArgument bb_arg);
  Instr copy(Instr instr);

  IntTypeR get_int_type(u16 bitwidth);
  FloatTypeR get_float_type(u16 bitwidth);

  VoidTypeR get_void_type();
  TypeR get_ptr_type();
  StructTypeR get_struct_type(IRVec<StructType::StructElem> elems);
  TypeR get_vec_type(fir::TypeR elem_ty, u16 n_lanes);

  template <class T>
  static void print_stats_vec(const utils::StableVec<T> &vec) {
    const auto n_slabs = vec.n_slabs();
    const auto slab_size = vec.slab_size();
    const auto size = vec.size_bytes();
    fmt::println("Vec NSlabs {} @ {} * {} = {} KB", n_slabs, slab_size,
                 sizeof(utils::Slot<T>), size / 1000);
    fmt::println("Space: {} Used: {}", n_slabs * slab_size, vec.n_used());
  }

  void print_stats() const;

  FunctionTypeR get_func_ty(TypeR ret_type, IRVec<TypeR> args);
  ConstantValueR get_poisson_value(TypeR type);
  ConstantValueR get_constant_null();
  ConstantValueR get_constant_value(FunctionR func);
  ConstantValueR get_constant_value(ConstantValue constant);
  ConstantValueR get_constant_value(f32 val, FloatTypeR ty);
  ConstantValueR get_constant_value(f64 val, FloatTypeR ty);
  ConstantValueR get_constant_value(u64 val, IntTypeR ty);
  ConstantValueR get_constant_value(i64 val, IntTypeR ty);
  ConstantValueR get_constant_value(i32 val, IntTypeR ty);
  ConstantValueR get_constant_value(i128 val, IntTypeR ty);
  ConstantValueR get_constant_int(i128 val, u32 bitwidth);
  ConstantValueR get_constant_value(u32 val, IntTypeR ty);
  ConstantValueR get_constant_value(IRVec<ConstantValueR> data, TypeR vec_ty);
  ConstantValueR try_reuse_constant(const ConstantValue &val);
  TypeR try_reuse_type(const AnyType &val);
  ConstantValueR get_constant_value(Global glob);

  bool has_global(IRString name) const;
  Global get_global(IRString name);
  Global insert_global(IRString name, size_t size_bytes);
  FunctionR get_function(IRStringRef name);
  bool has_function(IRStringRef name) const;
  FunctionR create_function(IRString name, FunctionTypeR type);
  // deletes funtion out of module returns true if function was found and
  // deleted
  bool delete_function(IRStringRef func);
  bool verify() const;
  void dump_graph(const char* filename);
};

class Context {

public:
  ContextData *data;
  Context() { data = new ContextData{}; }
  // ~Context() { delete data; }

  ContextData *operator->() const { return data; }
  void free() {
    delete data;
    data = nullptr;
  }
};

} // namespace foptim::fir

template <>
class fmt::formatter<foptim::fir::Context>
    : public BaseIRFormatter<foptim::fir::Context> {
public:
  appender format(foptim::fir::Context const &v,
                  format_context &ctx) const;
};
