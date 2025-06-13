#include "slp_vectorizer.hpp"
#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/value.hpp"
#include "utils/arena.hpp"
#include <fmt/core.h>

namespace foptim::optim {

class TreeElem {
public:
  TVec<TreeElem *> children;
  fir::Instr insert_loc;

  TreeElem() = default;
  virtual fir::ValueR generate(fir::Context & /*ctx*/) { TODO("UNREACH"); }
  virtual void dump() { TODO("UNREACH"); }
  virtual ~TreeElem() = default;
};

class BroadcastTreeOp final : public TreeElem {
  fir::ValueR v;

public:
  void dump() final { fmt::print("BROAD({})", v); }

  BroadcastTreeOp *init(const TVec<fir::ValueR> &values) {
    if (values.back().is_bb_arg()) {
      insert_loc = values.back().as_bb_arg()->_parent->instructions[0];
    } else {
      insert_loc = values.back().as_instr();
    }
    v = values.back();
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values) {
    auto base_v = values.back();
    if (base_v.is_constant()) {
      return false;
    }
    for (auto v : values) {
      if (v != base_v) {
        return false;
      }
    }
    return true;
  }

  fir::ValueR generate(fir::Context & /*ctx*/) final {
    fir::Builder bb{insert_loc};
    return bb.build_vbroadcast(v, insert_loc->get_type());
  }
};

class BinaryTreeOp final : public TreeElem {
public:
  void dump() final {
    children.at(1)->dump();
    switch ((fir::BinaryInstrSubType)insert_loc->subtype) {
    case fir::BinaryInstrSubType::FloatAdd:
    case fir::BinaryInstrSubType::IntAdd:
      fmt::print(" + ");
      break;
    default:
      fmt::print(" B ");
      break;
    }
    children.at(0)->dump();
  }

  BinaryTreeOp *init(const TVec<fir::ValueR> &values) {
    insert_loc = values.back().as_instr();
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values) {
    auto base_v = values.back().as_instr();
    bool potential_neutral_elem = false;
    for (auto i_v : values) {
      if (i_v == base_v->args[0]) {
        potential_neutral_elem = true;
        continue;
      }
      if (!i_v.is_instr()) {
        return false;
      }
      auto i = i_v.as_instr();
      if (i->instr_type != base_v->instr_type ||
          i->subtype != base_v->subtype) {
        return false;
      }
    }

    switch ((fir::BinaryInstrSubType)base_v->subtype) {
    case fir::BinaryInstrSubType::FloatAdd:
    case fir::BinaryInstrSubType::IntAdd:
      return true;
    case fir::BinaryInstrSubType::FloatSub:
    case fir::BinaryInstrSubType::FloatMul:
    case fir::BinaryInstrSubType::IntSub:
    case fir::BinaryInstrSubType::IntMul:
      break;
    case fir::BinaryInstrSubType::INVALID:
    case fir::BinaryInstrSubType::IntSRem:
    case fir::BinaryInstrSubType::IntSDiv:
    case fir::BinaryInstrSubType::IntUDiv:
    case fir::BinaryInstrSubType::Shl:
    case fir::BinaryInstrSubType::Shr:
    case fir::BinaryInstrSubType::AShr:
    case fir::BinaryInstrSubType::And:
    case fir::BinaryInstrSubType::Or:
    case fir::BinaryInstrSubType::Xor:
    case fir::BinaryInstrSubType::FloatDiv:
      return false;
    }
    // we cant handle neutral elemtns for everything
    return !potential_neutral_elem;
  }
  fir::ValueR generate(fir::Context &ctx) final {
    auto av = children.at(1)->generate(ctx);
    auto bv = children.at(0)->generate(ctx);
    fir::Builder bb{insert_loc};
    return bb.build_binary_op(av, bv,
                              (fir::BinaryInstrSubType)insert_loc->subtype);
  }
};

class StoreTreeOp final : public TreeElem {
  fir::Instr store_loc;

public:
  void dump() final {
    children.at(1)->dump();
    fmt::print(" = ");
    children.at(0)->dump();
    fmt::println("\n");
  }

  StoreTreeOp *init(const TVec<fir::ValueR> &values) {
    store_loc = values[0].as_instr();
    insert_loc = values.back().as_instr();
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values) {
    // assuming it works out ?
    (void)values;
    return true;
  }
  fir::ValueR generate(fir::Context &ctx) final {
    // TODO: assuming continious stores
    auto val = children.at(0)->generate(ctx);
    fir::Builder bb{insert_loc};
    return bb.build_store(store_loc->args[0], val);
  }
};

class ConstantTreeOp final : public TreeElem {
  TVec<fir::ValueR> my_values;

public:
  void dump() final {
    fmt::print("<");
    for (auto v : my_values) {
      fmt::print("{}", v);
    }
    fmt::print(">");
  }

  ConstantTreeOp *init(const TVec<fir::ValueR> &values, TreeElem *parent) {
    insert_loc = parent->insert_loc;
    my_values = values;
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values) {
    auto base_v = values.back().as_constant();
    for (auto i_v : values) {
      if (!i_v.is_constant()) {
        return false;
      }
      if (i_v.get_type() != base_v->get_type()) {
        return false;
      }
    }

    return true;
  }

  fir::ValueR generate(fir::Context &ctx) final {
    auto n_elems = my_values.size();
    IRVec<fir::ConstantValueR> args;

    for (size_t i = 0; i < n_elems; i++) {
      args.push_back(my_values[i].as_constant());
    }
    bool all_equal = true;
    for (size_t i = 1; i < args.size(); i++) {
      if (args[i] != args[0]) {
        all_equal = false;
        break;
      }
    }
    auto vec_ty = ctx->get_vec_type(args.at(0)->get_type(), n_elems);
    if (all_equal) {
      fir::Builder b{insert_loc};
      return fir::ValueR{b.build_vbroadcast(fir::ValueR{args[0]}, vec_ty)};
    }
    auto constant_data = ctx->get_constant_value(std::move(args), vec_ty);
    return fir::ValueR{constant_data};
  }
};

class LoadTreeOp final : public TreeElem {
  fir::Instr base_load;
  u16 n_lanes = 0;

public:
  void dump() final {
    fmt::print("L(");
    children.at(0)->dump();
    fmt::print(")");
  }

  LoadTreeOp *init(const TVec<fir::ValueR> &values) {
    base_load = values[0].as_instr();
    insert_loc = values.back().as_instr();
    n_lanes = values.size();
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values,
                    const TVec<SLPVectorizer::StoreLoadBundle> &load_bundles) {
    auto base_v = values.back().as_instr();
    for (auto i_v : values) {
      if (!i_v.is_instr()) {
        return false;
      }
      auto i = i_v.as_instr();
      if (i->instr_type != base_v->instr_type ||
          i->subtype != base_v->subtype) {
        return false;
      }
    }
    for (const auto &load_bundle : load_bundles) {
      if (load_bundle.data[0].instr == values[0].as_instr()) {
        return true;
      }
    }
    return false;
  }
  fir::ValueR generate(fir::Context &ctx) final {
    // TODO: assuming continious loads
    //  auto a = children[0]->generate(ctx);
    fir::Builder bb{insert_loc};
    auto vec_ty = ctx->get_vec_type(insert_loc->get_type(), n_lanes);
    return bb.build_load(vec_ty, base_load->args[0]);
  }
};

bool SLPVectorizer::tree_vectorize(fir::Context &ctx, StoreLoadBundle &b,
                                   const TVec<StoreLoadBundle> &load_bundles) {
  (void)ctx;

  using StoreAlloc = utils::TempAlloc<StoreTreeOp>;
  using LoadAlloc = utils::TempAlloc<LoadTreeOp>;
  using BinaryAlloc = utils::TempAlloc<BinaryTreeOp>;
  using ConstAlloc = utils::TempAlloc<ConstantTreeOp>;
  using BroadcastAlloc = utils::TempAlloc<BroadcastTreeOp>;
  TVec<TreeElem *> tree;
  TVec<std::pair<TreeElem *, TVec<fir::ValueR>>> worklist;
  {
    TVec<fir::ValueR> v;
    for (auto &va : b.data) {
      v.emplace_back(va.instr);
    }
    worklist.emplace_back(nullptr, std::move(v));
  }

  while (!worklist.empty()) {
    auto [parent, curr] = std::move(worklist.back());
    worklist.pop_back();
    auto test_i = curr.back();

    // if they are all the same it can just be a broadcast
    if (BroadcastTreeOp::match(curr)) {
      auto *result = BroadcastAlloc{}.allocate(1);
      (new (result) BroadcastTreeOp)->init(curr);
      ASSERT(parent != nullptr);
      parent->children.push_back(result);
      tree.push_back(result);
      continue;
    }

    if (test_i.is_instr()) {
      TreeElem *result;
      size_t n_args = 999999;
      switch (test_i.as_instr()->instr_type) {
      case fir::InstrType::StoreInstr:
        if (StoreTreeOp::match(curr)) {
          auto *result_t = StoreAlloc{}.allocate(1);
          (new (result_t) StoreTreeOp)->init(curr);
          result = result_t;
          n_args = 2;
          ASSERT(parent == nullptr);
        } else {
          fmt::println("Failed tree vectorize at store {}", curr[0]);
          return false;
        }
        break;
      case fir::InstrType::BinaryInstr:
        if (BinaryTreeOp::match(curr)) {
          auto *result_t = BinaryAlloc{}.allocate(1);
          (new (result_t) BinaryTreeOp)->init(curr);
          result = result_t;
          n_args = 2;
          ASSERT(parent != nullptr);
          parent->children.push_back(result);
          tree.push_back(result);

          auto arg_0 = test_i.as_instr()->args[0];
          for (size_t arg_id = 0; arg_id < n_args; arg_id++) {
            TVec<fir::ValueR> data;
            for (auto c : curr) {
              if (c == arg_0 && arg_id == 1 && test_i != c) {
                auto t = test_i.as_instr()->args[1].get_type();
                if (t->is_float()) {
                  // TODO THis is only correct ofr addition
                  data.emplace_back(ctx->get_constant_value(0.F, t));
                } else {
                  data.emplace_back(ctx->get_constant_value(0, t));
                }
              } else if (c == arg_0 && arg_id == 0 && test_i != c) {
                data.push_back(c);
              } else {
                data.push_back(c.as_instr()->args[arg_id]);
              }
            }
            worklist.emplace_back(result, std::move(data));
          }
          continue;
        } else {
          fmt::println("Failed tree vectorize at binary {}", curr[0]);
          return false;
        }
      case fir::InstrType::LoadInstr:
        if (LoadTreeOp::match(curr, load_bundles)) {
          auto *result_t = LoadAlloc{}.allocate(1);
          (new (result_t) LoadTreeOp)->init(curr);
          result = result_t;
          n_args = 1;
          parent->children.push_back(result);
        } else {
          fmt::println("Failed tree vectorize at load {}", curr[0]);
          return false;
        }
        break;
      case fir::InstrType::ICmp:
      case fir::InstrType::FCmp:
      case fir::InstrType::UnaryInstr:
      case fir::InstrType::AllocaInstr:
      case fir::InstrType::ExtractValue:
      case fir::InstrType::InsertValue:
      case fir::InstrType::ITrunc:
      case fir::InstrType::ZExt:
      case fir::InstrType::SExt:
      case fir::InstrType::Conversion:
      case fir::InstrType::SelectInstr:
      case fir::InstrType::CallInstr:
      case fir::InstrType::ReturnInstr:
      case fir::InstrType::BranchInstr:
      case fir::InstrType::CondBranchInstr:
      case fir::InstrType::SwitchInstr:
      case fir::InstrType::Unreachable:
      case fir::InstrType::Intrinsic:
      case fir::InstrType::VectorInstr:
        fmt::println("Failed tree vectorize at something like {}",
                     curr.back().as_instr());
        return false;
      }
      tree.push_back(result);
      for (size_t arg_id = 0; arg_id < n_args; arg_id++) {
        TVec<fir::ValueR> data;
        for (auto c : curr) {
          data.push_back(c.as_instr()->args[arg_id]);
        }
        worklist.emplace_back(result, std::move(data));
      }
      continue;
    }
    if (test_i.is_constant()) {
      if (ConstantTreeOp::match(curr)) {
        auto *result = ConstAlloc{}.allocate(1);
        (new (result) ConstantTreeOp)->init(curr, parent);
        parent->children.push_back(result);
        tree.push_back(result);
      } else {
        fmt::println("constant tree op no match");
        return false;
      }
      continue;
    }
    fmt::println("Failed tree vectorize at something like {}", curr[0]);
    return false;
  }

  // fmt::println("TreeSize {}", tree.size());
  if (!tree.empty()) {
    tree[0]->generate(ctx);
  }
  // auto par = b.data[0].instr->parent->get_parent();
  for (auto &bu : b.data) {
    bu.instr.destroy();
  }
  return true;
}

void SLPVectorizer::continious_vector_store(StoreLoadBundle &bundle,
                                            fir::ValueR value) {
  TVec<i128> offsets;
  // collect constant offsets
  for (const auto &data : bundle.data) {
    if (!data.instr->args[1].is_constant()) {
      ASSERT(false);
    }
    if (!data.a.is_invalid() ||
        (!data.b.is_invalid() &&
         (!data.b.is_constant() || !data.b.as_constant()->is_int()))) {
      ASSERT(false);
    }
    i128 consti = 0;
    if (data.b.is_constant()) {
      consti = data.b.as_constant()->as_int();
    }
    offsets.push_back(consti);
  }

  // check they are continious
  for (size_t i = 1; i < offsets.size(); i++) {
    if (offsets[i] - offsets[i - 1] != bundle.type->get_size()) {
      ASSERT(false);
    }
  }

  size_t last_indx = 0;
  fir::Instr last_instr;
  for (const auto &data : bundle.data) {
    auto bb = data.instr->parent;
    for (size_t i = 0; i < bb->instructions.size(); i++) {
      if (bb->instructions[i] == data.instr) {
        if (i > last_indx) {
          last_indx = i;
          last_instr = data.instr;
        }
        break;
      }
    }
  }

  fir::Builder bb{last_instr};
  bb.build_store(bundle.data[0].instr->args[0], value);
  for (auto &b : bundle.data) {
    b.instr.destroy();
  }
}

} // namespace foptim::optim
