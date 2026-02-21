#include "slp_vectorizer.hpp"

#include <fmt/base.h>
#include <fmt/core.h>

#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/value.hpp"
#include "utils/arena.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {

class BroadcastTreeOp final : public SLPVectorizer::TreeElem {
  fir::ValueR v;

 public:
  void dump() final { fmt::print("BROAD({})", v); }

  BroadcastTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
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

  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle & /*orig_bundle*/) final {
    fir::Builder bb{insert_loc};
    bb.after(insert_loc);
    auto res = bb.build_vbroadcast(
        v, ctx->get_vec_type(insert_loc->get_type(), n_lanes));
    return res;
  }
};

class HorizRedTreeOp final : public SLPVectorizer::TreeElem {
 public:
  void dump() final {
    fmt::print("RED(\n");
    for (auto *c : children) {
      fmt::print("  ");
      c->dump();
      fmt::print("\n");
    }
    fmt::print(")\n");
  }

  HorizRedTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
    if (values.back().is_bb_arg()) {
      insert_loc = values.back().as_bb_arg()->_parent->instructions[0];
    } else {
      insert_loc = values.back().as_instr();
    }
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values) {
    (void)values;
    TODO("prob cant match this idk?");
    return false;
  }

  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle &orig_bundle) final {
    fir::Builder bb{insert_loc};
    bb.after(insert_loc);
    ASSERT(children.size() == 1);

    auto subtype = orig_bundle.base.as_instr()->subtype;
    bool isProd = subtype == (u32)fir::BinaryInstrSubType::FloatMul ||
                  subtype == (u32)fir::BinaryInstrSubType::IntMul;
    bool isSum = subtype == (u32)fir::BinaryInstrSubType::FloatAdd ||
                 subtype == (u32)fir::BinaryInstrSubType::IntAdd;
    ASSERT(isProd || isSum);

    auto orig_red = orig_bundle.base.as_instr();
    auto vec = children.at(0)->generate(ctx, orig_bundle);
    auto new_v =
        bb.build_vector_op(vec, orig_red->get_type(),
                           isProd ? fir::VectorISubType::HorizontalMul
                                  : fir::VectorISubType::HorizontalAdd);
    orig_red->replace_all_uses(new_v);
    orig_red.destroy();
    return new_v;
  }
};

class BinaryTreeOp final : public SLPVectorizer::TreeElem {
 public:
  bool is_signed = false;
  void dump() final {
    children.at(1)->dump();
    switch ((fir::BinaryInstrSubType)insert_loc->subtype) {
      case fir::BinaryInstrSubType::FloatAdd:
      case fir::BinaryInstrSubType::IntAdd:
        fmt::print(" + ");
        break;
      case fir::BinaryInstrSubType::FloatMul:
      case fir::BinaryInstrSubType::IntMul:
        fmt::print(" * ");
        break;
      case fir::BinaryInstrSubType::FloatDiv:
      case fir::BinaryInstrSubType::IntSDiv:
      case fir::BinaryInstrSubType::IntUDiv:
        fmt::print(" / ");
        break;
      default:
        fmt::print(" BinOp ");
        break;
    }
    children.at(0)->dump();
  }

  BinaryTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
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
        fmt::println("One arg isnt a binary op");
        return false;
      }
      auto i = i_v.as_instr();
      if (i->instr_type != base_v->instr_type ||
          i->subtype != base_v->subtype) {
        fmt::println("One arg isnt the right instr");
        return false;
      }
    }
    if (base_v->args[1].get_type()->get_bitwidth() !=
        base_v->args[0].get_type()->get_bitwidth()) {
      return false;
    }
    switch ((fir::BinaryInstrSubType)base_v->subtype) {
      case fir::BinaryInstrSubType::FloatAdd:
      case fir::BinaryInstrSubType::IntAdd:
      case fir::BinaryInstrSubType::Shl:
        return true;
      case fir::BinaryInstrSubType::FloatSub:
      case fir::BinaryInstrSubType::FloatMul:
      case fir::BinaryInstrSubType::IntSub:
      case fir::BinaryInstrSubType::IntMul:
      case fir::BinaryInstrSubType::And:
      case fir::BinaryInstrSubType::Or:
      case fir::BinaryInstrSubType::Xor:
      case fir::BinaryInstrSubType::FloatDiv:
        // we cant handle neutral elemtns for everything
        return !potential_neutral_elem;
        // TODO. this causes issues + technically there is a neutral element for
        // div
      case fir::BinaryInstrSubType::IntSRem:
      case fir::BinaryInstrSubType::IntURem:
      case fir::BinaryInstrSubType::IntSDiv:
      case fir::BinaryInstrSubType::IntUDiv:
        return false;
      case fir::BinaryInstrSubType::INVALID:
      case fir::BinaryInstrSubType::Shr:
      case fir::BinaryInstrSubType::AShr:
        fmt::println("{}", base_v);
        TODO("impl");
        return false;
    }
  }
  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle &orig_bundl) final {
    auto av = children.at(0)->generate(ctx, orig_bundl);
    auto bv = children.at(1)->generate(ctx, orig_bundl);
    fir::Builder bb{insert_loc};
    auto res = bb.build_binary_op(av, bv,
                                  (fir::BinaryInstrSubType)insert_loc->subtype);
    return res;
  }
};

class ZextTreeOp final : public SLPVectorizer::TreeElem {
 public:
  fir::TypeR res_ty;
  void dump() final {
    children.at(1)->dump();
    fmt::print(" = ");
    children.at(0)->dump();
    fmt::println("\n");
  }

  ZextTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
    insert_loc = values.back().as_instr();
    res_ty = values.back().get_type();
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values) {
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
    return true;
  }

  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle &orig_bundl) final {
    auto val = children.at(0)->generate(ctx, orig_bundl);
    fir::Builder bb{insert_loc};
    return bb.build_zext(val, ctx->get_vec_type(res_ty, n_lanes));
  }
};

class UnaryTreeOp final : public SLPVectorizer::TreeElem {
 public:
  fir::UnaryInstrSubType sub_ty;
  void dump() final {
    children.at(1)->dump();
    fmt::print(" = ");
    children.at(0)->dump();
    fmt::println("\n");
  }

  UnaryTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
    insert_loc = values.back().as_instr();
    sub_ty = (fir::UnaryInstrSubType)values.back().as_instr()->subtype;
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values) {
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
    switch ((fir::UnaryInstrSubType)values.back().as_instr()->subtype) {
      case fir::UnaryInstrSubType::INVALID:
        return false;
      case fir::UnaryInstrSubType::IntNeg:
        fmt::println("{}", values.back().as_instr());
        TODO("impl it?");
      case fir::UnaryInstrSubType::Not:
      case fir::UnaryInstrSubType::FloatNeg:
      case fir::UnaryInstrSubType::FloatSqrt:
        break;
    }
    return true;
  }

  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle &orig_bundl) final {
    auto val = children.at(0)->generate(ctx, orig_bundl);
    fir::Builder bb{insert_loc};
    return bb.build_unary_op(val, sub_ty);
  }
};

class StoreTreeOp final : public SLPVectorizer::TreeElem {
  fir::Instr store_loc;

 public:
  void dump() final {
    children.at(1)->dump();
    fmt::print(" = ");
    children.at(0)->dump();
    fmt::println("\n");
  }

  StoreTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
    store_loc = values[0].as_instr();
    insert_loc = values.back().as_instr();
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values) {
    // assuming it works out ?
    (void)values;
    return true;
  }
  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle &orig_bundl) final {
    fmt::println("Gen Store slp");
    children.at(0)->dump();
    // auto *func = orig_bundl.data[0].instr->get_parent()->get_parent().func;
    // fmt::println(">>>>>>>>> {:cd}", *func);
    auto val = children.at(0)->generate(ctx, orig_bundl);
    // fmt::println("======== {:cd}", *func);
    fir::Builder bb{insert_loc};
    // TODO: assuming continious stores
    auto res = bb.build_store(store_loc->args[0], val, false, false);
    for (auto o : orig_bundl.data) {
      o.instr.destroy();
    }
    return res;
  }
};

class IntrinTreeOp final : public SLPVectorizer::TreeElem {
  fir::IntrinsicSubType type;

 public:
  void dump() final {
    switch (type) {
      default:
        TODO("UNREACH");
      case fir::IntrinsicSubType::Abs:
        fmt::print("abs(");
        children.at(0)->dump();
        fmt::print(")");
        break;
      case fir::IntrinsicSubType::FAbs:
        fmt::print("fabs(");
        children.at(0)->dump();
        fmt::print(")");
        break;
    }
  }

  IntrinTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
    insert_loc = values.back().as_instr();
    type = (fir::IntrinsicSubType)insert_loc->subtype;
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values) {
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

    switch ((fir::IntrinsicSubType)base_v->subtype) {
      case fir::IntrinsicSubType::INVALID:
      case fir::IntrinsicSubType::VA_start:
      case fir::IntrinsicSubType::VA_end:
      case fir::IntrinsicSubType::IsConstant:
        return false;
      case fir::IntrinsicSubType::CTLZ:
      case fir::IntrinsicSubType::FMin:
      case fir::IntrinsicSubType::FMax:
      case fir::IntrinsicSubType::UMin:
      case fir::IntrinsicSubType::UMax:
      case fir::IntrinsicSubType::SMin:
      case fir::IntrinsicSubType::SMax:
      case fir::IntrinsicSubType::Abs:
      case fir::IntrinsicSubType::FAbs:
      case fir::IntrinsicSubType::FRound:
      case fir::IntrinsicSubType::FCeil:
      case fir::IntrinsicSubType::FFloor:
      case fir::IntrinsicSubType::FTrunc:
        return true;
        break;
    }
  }

  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle &orig_bundl) final {
    // TODO: assuming continious stores
    auto val = children.at(0)->generate(ctx, orig_bundl);
    fir::Builder bb{insert_loc};
    switch (type) {
      default:
        TODO("UNREACH");
      case fir::IntrinsicSubType::CTLZ:
      case fir::IntrinsicSubType::UMin:
      case fir::IntrinsicSubType::UMax:
      case fir::IntrinsicSubType::SMin:
      case fir::IntrinsicSubType::SMax:
        TODO("impl");
      case fir::IntrinsicSubType::FMin:
        return bb.build_intrinsic(val, children.at(1)->generate(ctx, orig_bundl), fir::IntrinsicSubType::FMin);
      case fir::IntrinsicSubType::FMax:
        return bb.build_intrinsic(val, children.at(1)->generate(ctx, orig_bundl), fir::IntrinsicSubType::FMax);
      case fir::IntrinsicSubType::FRound:
        return bb.build_intrinsic(val, fir::IntrinsicSubType::FRound);
      case fir::IntrinsicSubType::FCeil:
        return bb.build_intrinsic(val, fir::IntrinsicSubType::FCeil);
      case fir::IntrinsicSubType::FFloor:
        return bb.build_intrinsic(val, fir::IntrinsicSubType::FFloor);
      case fir::IntrinsicSubType::Abs:
        return bb.build_abs(val);
      case fir::IntrinsicSubType::FAbs:
        return bb.build_fabs(val);
    }
  }
};

class ConstantTreeOp final : public SLPVectorizer::TreeElem {
  TVec<fir::ValueR> my_values;

 public:
  void dump() final {
    fmt::print("<");
    for (auto v : my_values) {
      fmt::print("{}", v);
    }
    fmt::print(">");
  }

  ConstantTreeOp *init(const TVec<fir::ValueR> &values,
                       SLPVectorizer::TreeElem *parent) {
    n_lanes = values.size();
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

  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle & /*orig_bundl*/) final {
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

class LoadTreeOp final : public SLPVectorizer::TreeElem {
  fir::Instr base_load;

 public:
  void dump() final {
    fmt::print("L(");
    children.at(0)->dump();
    fmt::print(")");
  }

  LoadTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
    base_load = values[0].as_instr();
    insert_loc = values.back().as_instr();
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values,
                    const TVec<SLPVectorizer::SeedBundle> &load_bundles) {
    auto base_v = values.back().as_instr();
    auto base_t = base_v->get_type();
    if (base_t->is_vec()) {
      // auto vec_loads = base_t->as_vec();
      // auto sub_data_size = vec_loads.bitwidth * vec_loads.member_number;
      // if (sub_data_size != 64 && sub_data_size != 32) {
      return false;
      // }
    }

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
      bool found_all = true;
      for (const auto &value : values) {
        bool inside_bundle = false;
        for (const auto &bundle_vals : load_bundle.data) {
          if (bundle_vals.instr == value.as_instr()) {
            inside_bundle = true;
            break;
          }
        }
        if (!inside_bundle) {
          found_all = false;
          break;
        }
      }
      if (found_all) {
        return true;
      }
    }
    return false;
  }

  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle &orig_bundl) final {
    // TODO: assuming continious loads
    //  auto a = children[0]->generate(ctx);
    auto val = children.at(0)->generate(ctx, orig_bundl);
    fir::Builder bb{insert_loc};
    if (val.is_instr()) {
      bb.after(val.as_instr());
    }
    if (insert_loc->get_type()->is_vec()) {
      auto vec_loads = insert_loc->get_type()->as_vec();
      auto vec_ty = ctx->get_vec_type(
          vec_loads.type, vec_loads.bitwidth * vec_loads.member_number,
          n_lanes);
      // auto real_vec_ty = ctx->get_vec_type(vec_loads.type,
      // vec_loads.bitwidth,
      //                                      n_lanes *
      //                                      vec_loads.member_number);
      auto res = bb.build_load(vec_ty, val, false, false);
      return res;
    }
    auto vec_ty = ctx->get_vec_type(insert_loc->get_type(), n_lanes);
    auto res = bb.build_load(vec_ty, val, false, false);
    return res;
  }
};

bool SLPVectorizer::tree_vectorize_reduction(
    fir::Context &ctx, SeedBundle &b, const TVec<SeedBundle> &load_bundles) {
  using HorizRedAlloc = utils::TempAlloc<HorizRedTreeOp>;

  ASSERT(b.data.size() % 2 == 0);
  auto *result_t = HorizRedAlloc{}.allocate(1);
  (new (result_t) HorizRedTreeOp)->init({b.base});
  return tree_vectorize(ctx, b, load_bundles, result_t);
}

bool SLPVectorizer::tree_vectorize(fir::Context &ctx, SeedBundle &b,
                                   const TVec<SeedBundle> &load_bundles,
                                   SLPVectorizer::TreeElem *default_parent) {
  (void)ctx;

  using StoreAlloc = utils::TempAlloc<StoreTreeOp>;
  using LoadAlloc = utils::TempAlloc<LoadTreeOp>;
  using BinaryAlloc = utils::TempAlloc<BinaryTreeOp>;
  using ConstAlloc = utils::TempAlloc<ConstantTreeOp>;
  using IntrinAlloc = utils::TempAlloc<IntrinTreeOp>;
  using BroadcastAlloc = utils::TempAlloc<BroadcastTreeOp>;
  using ZextTreeAlloc = utils::TempAlloc<ZextTreeOp>;
  using UnaryTreeAlloc = utils::TempAlloc<UnaryTreeOp>;
  TVec<TreeElem *> tree;
  if (default_parent != nullptr) {
    tree.push_back(default_parent);
  }
  TVec<std::pair<TreeElem *, TVec<fir::ValueR>>> worklist;
  {
    TVec<fir::ValueR> v;
    for (auto &va : b.data) {
      v.emplace_back(va.instr);
    }
    worklist.emplace_back(default_parent, std::move(v));
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
            fmt::println("Failed tree vectorize at binary {} {} {}",
                         test_i.as_instr(), curr[0], curr[1]);
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
        case fir::InstrType::Intrinsic:
          if (IntrinTreeOp::match(curr)) {
            auto *result_t = IntrinAlloc{}.allocate(1);
            (new (result_t) IntrinTreeOp)->init(curr);
            result = result_t;
            n_args = curr[0].as_instr()->get_n_args();
            parent->children.push_back(result);
          } else {
            fmt::println("Failed tree vectorize at intrinsic like {}",
                         curr.back().as_instr());
            return false;
          }
          break;
        case fir::InstrType::ZExt:
          if (ZextTreeOp::match(curr)) {
            auto *result_t = ZextTreeAlloc{}.allocate(1);
            (new (result_t) ZextTreeOp)->init(curr);
            result = result_t;
            n_args = 1;
            parent->children.push_back(result);
          } else {
            fmt::println("Failed tree vectorize at zext like {}",
                         curr.back().as_instr());
            return false;
          }
          break;
        case fir::InstrType::UnaryInstr:
          if (UnaryTreeOp::match(curr)) {
            auto *result_t = UnaryTreeAlloc{}.allocate(1);
            (new (result_t) UnaryTreeOp)->init(curr);
            result = result_t;
            n_args = 1;
            parent->children.push_back(result);
          } else {
            fmt::println("Failed tree vectorize at unary like {}",
                         curr.back().as_instr());
            return false;
          }
          break;
        case fir::InstrType::ITrunc:
        case fir::InstrType::ICmp:
        case fir::InstrType::FCmp:
          fmt::println("{}", curr.back().as_instr());
          TODO("Should be implementable");
        case fir::InstrType::SelectInstr:
        case fir::InstrType::AtomicRMW:
        case fir::InstrType::Fence:
        case fir::InstrType::VectorInstr:
        case fir::InstrType::SExt:
        case fir::InstrType::AllocaInstr:
        case fir::InstrType::ExtractValue:
        case fir::InstrType::InsertValue:
        case fir::InstrType::Conversion:
        case fir::InstrType::CallInstr:
        case fir::InstrType::ReturnInstr:
        case fir::InstrType::BranchInstr:
        case fir::InstrType::CondBranchInstr:
        case fir::InstrType::SwitchInstr:
        case fir::InstrType::Unreachable:
          fmt::println("Failed tree vectorize at instruction like {}",
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
    fmt::println("Failed tree vectorize at something like {:cd}", curr[0]);
    return false;
  }

  // tree[0]->dump();
  if (!tree.empty()) {
    // tree[0]->dump();
    // fmt::println("TreeSize {}", tree.size());
    tree[0]->generate(ctx, b);
  }
  utils::StatCollector::get().addi(1, "SLPVectorized",
                                   utils::StatCollector::StatFOptim);
  // auto par = b.data[0].instr->parent->get_parent();
  // for (auto &bu : b.data) {
  //   bu.instr.destroy();
  // }
  return true;
}

void SLPVectorizer::continious_vector_store(SeedBundle &bundle,
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
  bb.build_store(bundle.data[0].instr->args[0], value, false, false);
  for (auto &b : bundle.data) {
    b.instr.destroy();
  }
}

}  // namespace foptim::optim
