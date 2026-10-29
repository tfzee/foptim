#pragma once
#include <fmt/format.h>

#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "ir/access_meta.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/inline_asm.hpp"
#include "ir/intern_table.hpp"
#include "storage.hpp"
#include "types.hpp"
#include "types_ref.hpp"
#include "utils/logging.hpp"
#include "utils/types.hpp"

namespace foptim::optim {
class AliasSetTable;
}

namespace foptim::fir {

// defined next to the alias sets so the IR does not need them
struct AliasSetsDeleter {
  void operator()(optim::AliasSetTable *p) const;
};

struct ContextData {
  IRStorage storage = {};
  InternTable<ConstantValue> constant_table;
  InternTable<AnyType> type_table;
  using V2VMap = TMap<ValueR, ValueR>;

  // Alias sets shared by all functions, each function builds an overlay on
  // top. Only the loader and serial module passes add sets, so function
  // passes can read it in parallel.
  optim::AliasSetTable &alias_sets();

  // Inline asm statements of the module, the id is operand 0 of the InlineAsm
  // intrinsics. Safe to call from parallel jobs, entries are never removed and
  // the returned reference stays valid.
  u32 intern_asm(AsmInfo info);
  const AsmInfo &asm_info(u32 id);

  // dest is the function the copy ends up in, null for the function of bb.
  // The access metadata is copied as far as meta allows.
  BasicBlock copy(BasicBlock bb, V2VMap &subs, bool apply_subs = true,
                  MetaCopy meta = MetaCopy::All, Function *dest = nullptr);
  BBArgument copy(BBArgument bb_arg);
  // the copy has no access metadata
  Instr copy(Instr instr);

  IntTypeR get_int_type(u16 bitwidth);
  FloatTypeR get_float_type(u16 bitwidth);

  VoidTypeR get_void_type();
  TypeR get_ptr_type();
  TypeR get_vec_type(fir::TypeR elem_ty, u16 n_lanes);
  TypeR get_vec_type(fir::VectorType::SubType elem_ty, u16 width, u16 n_lanes);

  template <class T>
  static void print_stats_vec(const utils::StableVec<T> &vec) {
    const auto n_slabs = vec.nslabs();
    const auto slab_size = vec.slab_size();
    const auto size = vec.size_bytes();
    fmt::println("Vec NSlabs {} @ {} * {} = {} KB", n_slabs, slab_size,
                 sizeof(utils::Slot<T>), size / 1000);
    fmt::println("Space: {} Used: {}", n_slabs * slab_size, vec.n_used());
  }

  void print_stats() const;

  // single result (void for none)
  FunctionTypeR get_func_ty(TypeR ret_type, IRVec<TypeR> args);
  FunctionTypeR get_func_ty(std::span<const TypeR> ret_types,
                            IRVec<TypeR> args);
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
  // the width is the one of the value
  ConstantValueR get_constant_int(APInt val);
  ConstantValueR get_constant_value(u32 val, IntTypeR ty);
  ConstantValueR get_constant_value(IRVec<ConstantValueR> data, TypeR vec_ty);
  // returns the one constant / type with this value, creating it if needed
  ConstantValueR intern_constant(const ConstantValue &val);
  TypeR intern_type(const AnyType &val);
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

 private:
  std::mutex asm_mtx;
  std::deque<AsmInfo> asm_infos;
  std::unordered_multimap<u64, u32> asm_ids;
  std::once_flag alias_sets_once;
  std::unique_ptr<optim::AliasSetTable, AliasSetsDeleter> alias_sets_;

 public:
  void dump_graph(const char *filename);
};

class Context {
 public:
  ContextData *data;
  const conf::CompConf *config;
  Context(conf::CompConf *config) : config(config) { data = new ContextData{}; }
  // ~Context() { delete data; }

  ContextData *operator->() const { return data; }
  void free() {
    delete data;
    data = nullptr;
  }
};

}  // namespace foptim::fir

template <>
class fmt::formatter<foptim::fir::Context>
    : public BaseIRFormatter<foptim::fir::Context> {
 public:
  appender format(foptim::fir::Context const &v, format_context &ctx) const;
};
