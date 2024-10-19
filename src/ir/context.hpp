#pragma once
#include "ir/constant_value_ref.hpp"
#include "storage.hpp"
#include "types.hpp"
#include "types_ref.hpp"
#include "utils/logging.hpp"
#include "utils/stable_vec.hpp"

namespace foptim::fir {

struct ContextData {
  IRStorage storage = {};

  Instr copy(Instr instr) {
    auto res = storage.insert_instr(*instr.get_raw_ptr());
    return res;
  }

  TypeR copy(TypeR typee) {
    //FIXME: do this recusively to the subtypes
    auto res = storage.insert_type(*typee.get_raw_ptr());
    return res;
  }

  IntTypeR get_int_type(u16 bitwidth) {
    return storage.insert_type(IntegerType{bitwidth});
  }

  VoidTypeR get_void_type() { return storage.insert_type(VoidType{}); }
  VoidTypeR get_ptr_type() { return storage.insert_type(OpaquePointerType{}); }

  template <class T>
  static void print_stats_vec(const utils::FStableVec<T> &vec) {
    const auto n_slabs = vec.n_slabs();
    const auto slab_size = vec.slab_size();
    const auto size = vec.size_bytes();
    utils::Debug << "Vec NSlabs " << n_slabs << " @ " << slab_size << " * "
                 << sizeof(utils::Slot<T>) << " = " << size / 1000 << "KB\n";
    utils::Debug << "    Space: " << n_slabs * slab_size
                 << " Used: " << vec.n_used() << "\n";
  }

  void print_stats() const {
    utils::Debug << "Funcs " << storage.functions.size() << "\n";
    utils::Debug << "Instr ";
    print_stats_vec(storage.storage_instr);
    utils::Debug << "BBs ";
    print_stats_vec(storage.basic_blocks);
    utils::Debug << "Constant ";
    print_stats_vec(storage.storage_constant);
    utils::Debug << "Type ";
    print_stats_vec(storage.storage_type);
    utils::Debug << "Global ";
    print_stats_vec(storage.storage_global);
  }

  FunctionTypeR get_func_ty(TypeR ret_type, FVec<TypeR> args) {
    return storage.insert_type(FunctionType{ret_type, args});
  }

  ConstantValueR get_constant_value(u64 val, IntTypeR ty) {
    return storage.insert_constant(ConstantValue(val, ty));
  }

  ConstantValueR get_constant_value(Global glob) {
    //NOTE: Idk if this should be static if we add some ptr attribs idk?
    static auto global_ptr_typee =
        storage.insert_type(AnyType{OpaquePointerType()});
    return storage.insert_constant(ConstantValue(glob, global_ptr_typee));
  }

  Global get_global(size_t size_bytes) {
    return storage.insert_global(
        {size_bytes, ConstantValueR{ConstantValueR::invalid()}});
  }

  FunctionR create_function(std::string name, FunctionTypeR type) {
    storage.functions.insert({name, Function{this, name, type}});

    FunctionR func = FunctionR(&storage.functions.at(name));
    auto init_bb = BasicBlock(storage.basic_blocks.push_back({func}));
    init_bb.verify_validness();
    func->append_bbr(init_bb);
    func->set_entry_bbr(init_bb);

    return func;
  }

  bool verify() const {
    auto printer = utils::Debug;
    for (const auto &[name, func] : storage.functions) {
      if (!func.verify(printer.pad(1))) {
        printer << "In Function: " << name.c_str() << "\n";
        return false;
      }
    }
    return true;
  }
};

class Context {

public:
  ContextData *data;
  Context() { data = new ContextData{}; }
  ~Context() { delete data; }

  ContextData *operator->() { return data; }
  const ContextData *operator->() const { return data; }
};
} // namespace foptim::fir
