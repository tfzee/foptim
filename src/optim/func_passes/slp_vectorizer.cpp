#include "slp_vectorizer.hpp"

#include <fmt/base.h>
#include <fmt/core.h>

#include <algorithm>

#include "ir/basic_block_ref.hpp"
#include "ir/builder.hpp"
#include "ir/constant_value_ref.hpp"
#include "ir/instruction.hpp"
#include "ir/instruction_data.hpp"
#include "ir/types.hpp"
#include "ir/value.hpp"
#include "optim/helper/WFVector.hpp"
#include "utils/arena.hpp"
#include "utils/parameters.hpp"
#include "utils/stats.hpp"

namespace foptim::optim {

class BroadcastTreeOp final : public SLPVectorizer::TreeElem {
  fir::ValueR v;
  bool after;

 public:
  void dump() final { fmt::print("BROAD({})", v); }

  BroadcastTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
    if (values.back().is_bb_arg()) {
      insert_loc = values.back().as_bb_arg()->_parent->instructions[0];
      after = false;
    } else {
      insert_loc = values.back().as_instr();
      after = true;
    }
    v = values.back();
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values) {
    auto base_v = values.back();
    if (base_v.is_constant()) {
      return false;
    }
    auto ty = base_v.get_type();
    if (!ty->is_int() && !ty->is_ptr() && !ty->is_float()) {
      return false;
    }
    for (auto v : values) {
      if (v != base_v) {
        return false;
      }
    }
    return true;
  }

  i64 cost() const final { return -1; }

  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle & /*orig_bundle*/) final {
    fir::Builder bb{insert_loc};
    if (after) {
      bb.after(insert_loc);
    }
    auto res = bb.build_vbroadcast(v, ctx->get_vec_type(v.get_type(), n_lanes));
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

  i64 cost() const final { return children.at(0)->cost() + -(n_lanes / 2); }

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
    auto orig_instr = orig_bundle.base.as_instr();
    auto subtype = orig_instr->subtype;
    ASSERT(children.size() == 1);

    bool isProd = subtype == (u32)fir::BinaryInstrSubType::FloatMul ||
                  subtype == (u32)fir::BinaryInstrSubType::IntMul;
    bool isSum = subtype == (u32)fir::BinaryInstrSubType::FloatAdd ||
                 subtype == (u32)fir::BinaryInstrSubType::IntAdd;
    ASSERT(isProd || isSum);

    auto vec = children.at(0)->generate(ctx, orig_bundle);
    fir::Builder bb{orig_instr};
    bb.after(orig_instr);
    auto new_v =
        bb.build_vector_op(vec, orig_instr->get_type(),
                           isProd ? fir::VectorISubType::HorizontalMul
                                  : fir::VectorISubType::HorizontalAdd);
    orig_instr->replace_all_uses(new_v);
    orig_instr.destroy();
    return new_v;
  }
};

class BinaryTreeOp final : public SLPVectorizer::TreeElem {
 public:
  bool is_signed = false;
  fir::BinaryInstrSubType binary_op = fir::BinaryInstrSubType::INVALID;
  fir::TypeR orig_type;

  void dump() final {
    children.at(1)->dump();
    switch (binary_op) {
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

  i64 cost() const final {
    auto cost = children.at(0)->cost() + children.at(1)->cost() + n_lanes;
    if (binary_op == fir::BinaryInstrSubType::IntMul &&
        !utils::enable_avx512dq && orig_type->is_int()) {
      cost -= 8;
    }
    return cost;
  }

  BinaryTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
    if (values.back().is_instr()) {
      insert_loc = values.back().as_instr();
      binary_op = (fir::BinaryInstrSubType)insert_loc->subtype;
      orig_type = insert_loc->get_type();
    } else if (values.back().is_bb_arg()) {
      insert_loc = values.back().as_bb_arg()->get_parent()->instructions[0];
      binary_op = (fir::BinaryInstrSubType)values.front().as_instr()->subtype;
      orig_type = values.front().as_instr()->get_type();
    } else {
      insert_loc = values.front().as_instr();
      binary_op = (fir::BinaryInstrSubType)insert_loc->subtype;
      orig_type = insert_loc->get_type();
    }
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values,
                    bool &potential_neutral_elem) {
    fir::Instr base_v{fir::Instr::invalid()};
    if (values.back().is_instr()) {
      base_v = values.back().as_instr();
    } else {
      base_v = values.front().as_instr();
    }
    for (auto i_v : values) {
      if (i_v == base_v->args[0]) {
        potential_neutral_elem = true;
        continue;
      }
      if (!i_v.is_instr()) {
        if constexpr (SLPVectorizer::debug_print) {
          fmt::println("One arg isnt a binary op");
        }
        return false;
      }
      auto i = i_v.as_instr();
      if (i->instr_type != base_v->instr_type ||
          i->subtype != base_v->subtype) {
        if constexpr (SLPVectorizer::debug_print) {
          fmt::println("One arg isnt the right instr");
        }
        return false;
      }
    }
    if (base_v->args[1].get_type()->get_bitwidth() !=
        base_v->args[0].get_type()->get_bitwidth()) {
      if constexpr (SLPVectorizer::debug_print) {
        fmt::println("Wrong bitwidth");
      }
      return false;
    }
    switch ((fir::BinaryInstrSubType)base_v->subtype) {
      case fir::BinaryInstrSubType::FloatAdd:
      case fir::BinaryInstrSubType::IntAdd:
      case fir::BinaryInstrSubType::Shl:
      case fir::BinaryInstrSubType::IntSub:
      case fir::BinaryInstrSubType::Xor:
      case fir::BinaryInstrSubType::Or:
        return true;
      case fir::BinaryInstrSubType::FloatSub:
      case fir::BinaryInstrSubType::FloatMul:
      case fir::BinaryInstrSubType::IntMul:
      case fir::BinaryInstrSubType::And:
      case fir::BinaryInstrSubType::FloatDiv:
        if (SLPVectorizer::debug_print && potential_neutral_elem) {
          fmt::println(
              "Failed with op that doesnt support neutral but needs it");
        }
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
    auto bv = children.at(0)->generate(ctx, orig_bundl);
    auto av = children.at(1)->generate(ctx, orig_bundl);
    fir::Builder bb{insert_loc};
    auto res = bb.build_binary_op(av, bv, binary_op);
    return res;
  }
};

class ZextTreeOp final : public SLPVectorizer::TreeElem {
 public:
  fir::TypeR res_ty;
  void dump() final {
    // children.at(1)->dump();
    // fmt::print(" = ");
    children.at(0)->dump();
    fmt::println("\n");
  }

  i64 cost() const final { return children.at(0)->cost() + n_lanes * 1; }

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

class ITruncTreeOp final : public SLPVectorizer::TreeElem {
 public:
  fir::TypeR res_ty;
  void dump() final {
    // children.at(1)->dump();
    fmt::print("itrunc(");
    children.at(0)->dump();
    fmt::println(")");
  }

  i64 cost() const final { return children.at(0)->cost() + n_lanes * 1; }

  ITruncTreeOp *init(const TVec<fir::ValueR> &values) {
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
    return bb.build_itrunc(val, ctx->get_vec_type(res_ty, n_lanes));
  }
};

class CallTreeOp final : public SLPVectorizer::TreeElem {
 public:
  fir::ValueR func_ref;
  void dump() final {
    fmt::print("call {} ", func_ref);
    for (auto &child : children) {
      child->dump();
    }
    fmt::println("\n");
  }

  CallTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
    insert_loc = values.back().as_instr();
    func_ref = values.back().as_instr()->args[0];
    return this;
  }

  i64 cost() const final {
    i64 cost = 0;
    for (auto &child : children) {
      cost += child->cost();
    }
    return cost;
  }

  static bool match(const TVec<fir::ValueR> &values) {
    fir::Instr base_v{fir::Instr::invalid()};
    if (values.back().is_instr()) {
      base_v = values.back().as_instr();
    } else {
      base_v = values.front().as_instr();
    }
    if (!base_v->args[0].is_constant_func()) {
      return false;
    }
    auto called_func = base_v->args[0].as_constant()->as_func();
    if (!called_func->attribs.maybe_can_wfvec) {
      return false;
    }
    for (auto i_v : values) {
      if (!i_v.is_instr()) {
        return false;
      }
      auto i = i_v.as_instr();
      if (!i->args[0].is_constant_func()) {
        return false;
      }
      if (i->args[0] != base_v->args[0]) {
        return false;
      }
      if (i->instr_type != base_v->instr_type ||
          i->subtype != base_v->subtype) {
        return false;
      }
    }
    return true;
  }

  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle &orig_bundl) final {
    TVec<fir::ValueR> args;
    args.resize(children.size() - 1);
    // get args other then function parameter
    for (size_t i = 0; i < children.size() - 1; i++) {
      args[children.size() - 2 - i] = children.at(i)->generate(ctx, orig_bundl);
    }
    fir::Builder bb{insert_loc};
    auto *funcy = func_ref.as_constant()->as_func().func;
    auto new_func = whole_function_vectorize(*funcy, n_lanes);
    ASSERT(new_func.has_value())
    auto ftype = new_func.value()->func_ty;
    auto res =
        bb.build_call(fir::ValueR{ctx->get_constant_value(new_func.value())},
                      ftype, ftype->as_func().return_type, args);
    // for(auto b : orig_bundl.data){
    //   b.instr.destroy();
    // }
    // fmt::println("{:cd}", res.as_instr()->get_parent());
    // TODO("impl call tree op generate");
    return res;
  }
};

class UnaryTreeOp final : public SLPVectorizer::TreeElem {
 public:
  fir::UnaryInstrSubType sub_ty;
  void dump() final {
    // children.at(1)->dump();
    // fmt::print(" = ");
    children.at(0)->dump();
    fmt::println("\n");
  }

  UnaryTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
    insert_loc = values.back().as_instr();
    sub_ty = (fir::UnaryInstrSubType)values.back().as_instr()->subtype;
    return this;
  }

  i64 cost() const final { return children.at(0)->cost() + n_lanes * 2; }

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

class ExtractTreeOp final : public SLPVectorizer::TreeElem {
 public:
  void dump() final { fmt::println("\n"); }

  ExtractTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
    insert_loc = values.back().as_instr();
    return this;
  }

  i64 cost() const final { return -2; }

  static bool match(const TVec<fir::ValueR> &values) {
    auto base_v = values.back().as_instr();
    auto exp_off = 0;
    auto input_val = base_v->args[0];
    for (auto i_v : values) {
      if (!i_v.is_instr()) {
        return false;
      }
      auto i = i_v.as_instr();
      if (i->instr_type != base_v->instr_type ||
          i->subtype != base_v->subtype) {
        return false;
      }
      if (!i->args[1].is_const_int(exp_off) || i->args[0] != input_val) {
        return false;
      }
      exp_off++;
    }
    return true;
  }

  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle &orig_bundl) final {
    (void)ctx;
    (void)orig_bundl;
    return insert_loc->args[0];
  }
};

class StoreTreeOp final : public SLPVectorizer::TreeElem {
  fir::Instr store_loc;

 public:
  void dump() final {
    // children.at(1)->dump();
    // fmt::print(" = ");
    children.at(0)->dump();
    fmt::println("\n");
  }

  i64 cost() const final { return children.at(0)->cost() + n_lanes * 2; }

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
    // fmt::println("Gen Store slp");
    // children.at(0)->dump();
    // auto *func = orig_bundl.data[0].instr->get_parent()->get_parent().func;
    // fmt::println(">>>>>>>>> {:cd}", *func);
    auto val = children.at(0)->generate(ctx, orig_bundl);
    // fmt::println("======== {:cd}", *func);
    fir::Builder bb{insert_loc};
    bb.after(insert_loc);
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

  i64 cost() const final {
    if (children.size() == 2) {
      return children.at(0)->cost() + children.at(1)->cost() + n_lanes * 2;
    } else {
      return children.at(0)->cost() + n_lanes * 1;
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
        if (SLPVectorizer::debug_print) {
          fmt::println("intrin constant fail");
        }
        return false;
      }
      auto i = i_v.as_instr();
      if (i->instr_type != base_v->instr_type ||
          i->subtype != base_v->subtype) {
        if (SLPVectorizer::debug_print) {
          fmt::println("intrin instr type fail");
        }
        return false;
      }
    }

    switch ((fir::IntrinsicSubType)base_v->subtype) {
      case fir::IntrinsicSubType::INVALID:
      case fir::IntrinsicSubType::VA_start:
      case fir::IntrinsicSubType::VA_end:
      case fir::IntrinsicSubType::IsConstant:
        return false;
      case fir::IntrinsicSubType::PopCnt:
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
        return bb.build_intrinsic(val,
                                  children.at(1)->generate(ctx, orig_bundl),
                                  fir::IntrinsicSubType::FMin);
      case fir::IntrinsicSubType::FMax:
        return bb.build_intrinsic(val,
                                  children.at(1)->generate(ctx, orig_bundl),
                                  fir::IntrinsicSubType::FMax);
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

  i64 cost() const final { return n_lanes * 1; }

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

class ConversionTreeOp final : public SLPVectorizer::TreeElem {
  fir::ConversionSubType sub_type;
  fir::TypeR orig_type;

 public:
  void dump() final {
    fmt::print("CONV(");
    fmt::print(")");
  }

  i64 cost() const final {
    // TODO switch on type of cast
    return 1;
  }

  ConversionTreeOp *init(const TVec<fir::ValueR> &values) {
    n_lanes = values.size();
    if (values.back().is_instr()) {
      insert_loc = values.back().as_instr();
      sub_type = (fir::ConversionSubType)insert_loc->subtype;
      orig_type = insert_loc->get_type();
    } else if (values.back().is_bb_arg()) {
      insert_loc = values.back().as_bb_arg()->get_parent()->instructions[0];
      sub_type = (fir::ConversionSubType)values.front().as_instr()->subtype;
      orig_type = values.front().as_instr()->get_type();
    } else {
      insert_loc = values.front().as_instr();
      sub_type = (fir::ConversionSubType)insert_loc->subtype;
      orig_type = insert_loc->get_type();
    }
    return this;
  }

  static bool match(const TVec<fir::ValueR> &values) {
    fir::Instr base_v{fir::Instr::invalid()};
    if (values.back().is_instr()) {
      base_v = values.back().as_instr();
    } else {
      base_v = values.front().as_instr();
    }
    for (auto i_v : values) {
      if (!i_v.is_instr()) {
        if constexpr (SLPVectorizer::debug_print) {
          fmt::println("One arg isnt a binary op");
        }
        return false;
      }
      auto i = i_v.as_instr();
      if (i->instr_type != base_v->instr_type ||
          i->subtype != base_v->subtype) {
        if constexpr (SLPVectorizer::debug_print) {
          fmt::println("One arg isnt the right instr");
        }
        return false;
      }
    }

    return true;
  }

  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle &orig_bundl) final {
    auto av = children.at(0)->generate(ctx, orig_bundl);
    fir::Builder bb{insert_loc};

    auto res = bb.build_conversion_op(av, ctx->get_vec_type(orig_type, n_lanes),
                                      sub_type);
    return res;
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

  i64 cost() const final { return children.at(0)->cost() + n_lanes * 1; }

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
      if (SLPVectorizer::debug_print) {
        fmt::println("failed load already vec");
      }
      return false;
      // }
    }

    for (auto i_v : values) {
      if (!i_v.is_instr()) {
        if (SLPVectorizer::debug_print) {
          fmt::println("failed load missing instr {}", i_v);
        }
        return false;
      }
      auto i = i_v.as_instr();
      if (i->instr_type != base_v->instr_type ||
          i->subtype != base_v->subtype) {
        if (SLPVectorizer::debug_print) {
          fmt::println("failed load missmatched instr type {}", i);
        }
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
    if (SLPVectorizer::debug_print) {
      fmt::println("failed load didnt find bundle");
    }
    return false;
  }

  fir::ValueR generate(fir::Context &ctx,
                       SLPVectorizer::SeedBundle &orig_bundl) final {
    auto val = children.at(0)->generate(ctx, orig_bundl);
    fir::Builder bb{insert_loc};
    bb.after(insert_loc);
    // fmt::println("====> {}\n{}\n====================", insert_loc,
    //              insert_loc->get_parent());
    if (insert_loc->get_type()->is_vec()) {
      auto vec_loads = insert_loc->get_type()->as_vec();
      auto vec_ty = ctx->get_vec_type(
          vec_loads.type, vec_loads.bitwidth * vec_loads.member_number,
          n_lanes);
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
  using ITruncTreeAlloc = utils::TempAlloc<ITruncTreeOp>;
  using UnaryTreeAlloc = utils::TempAlloc<UnaryTreeOp>;
  using ExtractTreeAlloc = utils::TempAlloc<ExtractTreeOp>;
  using CallTreeAlloc = utils::TempAlloc<CallTreeOp>;
  using ConversionTreeAlloc = utils::TempAlloc<ConversionTreeOp>;

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
    if (!test_i.is_instr() && curr.front().is_instr()) {
      test_i = curr.front();
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
            if constexpr (debug_print) {
              fmt::println("Failed tree vectorize at store {}", curr[0]);
            }
            return false;
          }
          break;
        case fir::InstrType::BinaryInstr: {
          bool potential_neutral_elem = false;
          if (BinaryTreeOp::match(curr, potential_neutral_elem)) {
            auto *result_t = BinaryAlloc{}.allocate(1);
            (new (result_t) BinaryTreeOp)->init(curr);
            result = result_t;
            n_args = 2;
            ASSERT(parent != nullptr);
            parent->children.push_back(result);
            tree.push_back(result);

            f64 neutral_float = 0.F;
            i128 neutral_int = 0;

            if (potential_neutral_elem) {
              switch (result_t->binary_op) {
                case fir::BinaryInstrSubType::FloatAdd:
                case fir::BinaryInstrSubType::IntAdd:
                case fir::BinaryInstrSubType::Shl:
                case fir::BinaryInstrSubType::IntSub:
                case fir::BinaryInstrSubType::Xor:
                case fir::BinaryInstrSubType::Or:
                  neutral_float = 0.F;
                  neutral_int = 0;
                  break;
                case fir::BinaryInstrSubType::IntMul:
                  neutral_float = 1.F;
                  neutral_int = 1;
                  break;
                case fir::BinaryInstrSubType::And:
                  neutral_float = std::bit_cast<f64>(~(u64)0);
                  neutral_int = ~(i128)0;
                  break;
                case fir::BinaryInstrSubType::INVALID:
                case fir::BinaryInstrSubType::IntSRem:
                case fir::BinaryInstrSubType::IntURem:
                case fir::BinaryInstrSubType::IntSDiv:
                case fir::BinaryInstrSubType::IntUDiv:
                case fir::BinaryInstrSubType::Shr:
                case fir::BinaryInstrSubType::AShr:
                case fir::BinaryInstrSubType::FloatSub:
                case fir::BinaryInstrSubType::FloatMul:
                case fir::BinaryInstrSubType::FloatDiv:
                  UNREACH();
              }
            }
            auto arg_0 = test_i.as_instr()->args[0];
            for (size_t arg_id = 0; arg_id < n_args; arg_id++) {
              TVec<fir::ValueR> data;
              for (auto c : curr) {
                if (c == arg_0 && arg_id == 1 && test_i != c) {
                  auto t = test_i.as_instr()->args[1].get_type();
                  if (t->is_float()) {
                    // TODO THis is only correct ofr addition
                    data.emplace_back(
                        ctx->get_constant_value(neutral_float, t));
                  } else {
                    data.emplace_back(ctx->get_constant_value(neutral_int, t));
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
            if constexpr (debug_print) {
              fmt::println("Failed tree vectorize at binary {} {} {}",
                           test_i.as_instr(), curr[0], curr[1]);
            }
            return false;
          }
        }
        case fir::InstrType::LoadInstr:
          if (LoadTreeOp::match(curr, load_bundles)) {
            auto *result_t = LoadAlloc{}.allocate(1);
            (new (result_t) LoadTreeOp)->init(curr);
            result = result_t;
            n_args = 1;
            parent->children.push_back(result);
          } else {
            if constexpr (debug_print) {
              fmt::println("Failed tree vectorize at load {}", curr[0]);
            }
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
            if constexpr (debug_print) {
              fmt::println("Failed tree vectorize at intrinsic like {}",
                           curr.back().as_instr());
            }
            return false;
          }
          break;
        case fir::InstrType::ITrunc:
          if (ZextTreeOp::match(curr)) {
            auto *result_t = ITruncTreeAlloc{}.allocate(1);
            (new (result_t) ITruncTreeOp)->init(curr);
            result = result_t;
            n_args = 1;
            parent->children.push_back(result);
          } else {
            if constexpr (debug_print) {
              fmt::println("Failed tree vectorize at itrunc like {}",
                           curr.back().as_instr());
            }
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
            if constexpr (debug_print) {
              fmt::println("Failed tree vectorize at zext like {}",
                           curr.back().as_instr());
            }
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
            if constexpr (debug_print) {
              fmt::println("Failed tree vectorize at unary like {}",
                           curr.back().as_instr());
            }
            return false;
          }
          break;
        case fir::InstrType::ExtractValue:
          if (ExtractTreeOp::match(curr)) {
            auto *result_t = ExtractTreeAlloc{}.allocate(1);
            (new (result_t) ExtractTreeOp)->init(curr);
            result = result_t;
            n_args = 0;
            parent->children.push_back(result);
          } else {
            if constexpr (debug_print) {
              fmt::println("Failed tree vectorize at extract value like {}",
                           curr.back().as_instr());
            }
            return false;
          }
          break;
        case fir::InstrType::CallInstr:
          if (CallTreeOp::match(curr)) {
            auto *result_t = CallTreeAlloc{}.allocate(1);
            (new (result_t) CallTreeOp)->init(curr);
            result = result_t;
            n_args = curr[0].as_instr()->get_n_args();
            parent->children.push_back(result);
          } else {
            if constexpr (debug_print) {
              fmt::println("Failed tree vectorize at call value like {}",
                           curr.back().as_instr());
            }
            return false;
          }
          break;
        case fir::InstrType::Conversion:
          if (ConversionTreeOp::match(curr)) {
            auto *result_t = ConversionTreeAlloc{}.allocate(1);
            (new (result_t) ConversionTreeOp)->init(curr);
            result = result_t;
            n_args = 1;
            parent->children.push_back(result);
          } else {
            if constexpr (debug_print) {
              fmt::println("Failed tree vectorize at conversion value like {}",
                           curr.back().as_instr());
            }
            return false;
          }
          break;
        case fir::InstrType::ICmp:
        case fir::InstrType::FCmp:
        case fir::InstrType::SelectInstr:
          fmt::println("{}", test_i.as_instr());
          TODO("Should be implementable");
        case fir::InstrType::AtomicRMW:
        case fir::InstrType::Fence:
        case fir::InstrType::VectorInstr:
        case fir::InstrType::SExt:
        case fir::InstrType::AllocaInstr:
        case fir::InstrType::InsertValue:
        case fir::InstrType::ReturnInstr:
        case fir::InstrType::BranchInstr:
        case fir::InstrType::CondBranchInstr:
        case fir::InstrType::SwitchInstr:
        case fir::InstrType::Unreachable:
          if constexpr (debug_print) {
            fmt::println("Failed tree vectorize at instruction like {}",
                         curr.back().as_instr());
          }
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
        if constexpr (debug_print) {
          fmt::println("constant tree op no match");
        }
        return false;
      }
      continue;
    }
    if constexpr (debug_print) {
      fmt::println("Failed tree vectorize at something like-> {:cd}",
                   curr.back());
    }
    return false;
  }

  if (!tree.empty()) {
    auto tree_cost = tree[0]->cost();
    if (tree_cost > 0) {
      // auto funccy = tree[0]->insert_loc->get_parent()->get_parent();
      // fmt::print("===================Generate START=================\n{:cd}",
      //            *funccy.func);
      // tree[0]->dump();
      tree[0]->generate(ctx, b);
      // fmt::print("{:cd}\n===================Generated END=================\n",
      //            *funccy.func);
    } else {
      if constexpr (debug_print) {
        fmt::println("Failed vectorize -> not worth");
        tree[0]->dump();
        fmt::println("Cost tree {}", tree_cost);
      }
    }
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

std::pair<SLPVectorizer::SeedInstrData, fir::ValueR>
SLPVectorizer::get_storeload_data(fir::Instr storeload) {
  SeedInstrData data;
  data.instr = storeload;
  if (!storeload->args[0].is_instr()) {
    return {data, storeload->args[0]};
  }
  auto arg = storeload->args[0].as_instr();
  if (arg->is(fir::InstrType::BinaryInstr) &&
      arg->subtype == (u32)fir::BinaryInstrSubType::IntAdd &&
      arg->args[1].is_constant()) {
    data.b = arg->args[1];
    return {data, arg->args[0]};
  }
  if (arg->is(fir::InstrType::BinaryInstr) &&
      arg->subtype == (u32)fir::BinaryInstrSubType::IntMul) {
    if (arg->args[0].is_instr() &&
        arg->args[0].as_instr()->is(fir::InstrType::BinaryInstr) &&
        arg->args[0].as_instr()->subtype ==
            (u32)fir::BinaryInstrSubType::IntAdd) {
      auto sub_val = arg->args[0].as_instr();
      data.a = arg->args[1];
      data.b = sub_val->args[1];
      return {data, sub_val->args[0]};
    }
    data.a = arg->args[1];
    return {data, arg->args[0]};
  }
  return {data, storeload->args[0]};
}

std::optional<SLPVectorizer::SeedBundle> SLPVectorizer::find_successive_loads(
    fir::BasicBlock bb, size_t instr_id, AliasAnalyis &aa) {
  SeedBundle curr;
  auto [data, base] = get_storeload_data(bb->instructions[instr_id]);
  curr.base = base;
  curr.type = bb->instructions[instr_id].get_type();
  curr.data.push_back(data);

  TVec<fir::Instr> pot_aliasing_stores;

  for (auto i = instr_id + 1; i < bb->instructions.size(); i++) {
    auto instr = bb->instructions[i];
    if (instr->is(fir::InstrType::LoadInstr) && curr.type == instr.get_type() &&
        !instr->Atomic) {
      auto [sdata, sbase] = get_storeload_data(instr);
      // if (curr.base == sbase) {
      curr.data.push_back(sdata);
      // }
    } else if (instr->is(fir::InstrType::StoreInstr)) {
      bool might_alias = false;
      for (const auto &load : curr.data) {
        if (aa.alias(instr->args[0], load.instr->args[0]) !=
            AliasAnalyis::AAResult::NoAlias) {
          might_alias = true;
          break;
        }
      }
      if (might_alias) {
        break;
      }
      pot_aliasing_stores.push_back(instr);
    } else if (instr->pot_modifies_mem()) {
      // TODO if it writes we could use aliasing to still apply this
      break;
    }
  }
  // if we alias with anything just abort for now
  // TODO: could just remove the aliasing loads from the set
  // for (auto pot_stor : pot_aliasing_stores) {
  //   for (u32 lid = curr.data.size(); lid > 0; lid--) {
  //     if (aa.alias(pot_stor->args[0], curr.data[lid - 1].instr->args[0]) !=
  //         AliasAnalyis::AAResult::NoAlias) {
  //       curr.data.erase(curr.data.begin() + lid);
  //     }
  //   }
  // }
  if (curr.data.size() > 1) {
    return curr;
  }
  return {};
}

namespace {
/*
    To have proper and consistent order and not accidentially causing the order
    to be flipped by collecting them wrongly i decided to just do left and right
    reduction so add(x, add(x, ...)) and add(add(x, ...), x) and not other tree
    like structures.
  */
void left_red_search(TVec<fir::ValueR> &reduction_inputs, fir::ValueR curr_base,
                     u32 exp_sub_type) {
  if (!curr_base.is_instr()) {
    reduction_inputs.clear();
    return;
  }
  auto curr_i = curr_base.as_instr();

  if (curr_i->instr_type != fir::InstrType::BinaryInstr ||
      curr_i->subtype != exp_sub_type) {
    reduction_inputs.push_back(curr_base);
    return;
  }
  if ((curr_i->args[0].is_instr() &&
       curr_i->args[0].as_instr()->instr_type == fir::InstrType::BinaryInstr &&
       curr_i->args[0].as_instr()->subtype == exp_sub_type)) {
    left_red_search(reduction_inputs, curr_i->args[0], exp_sub_type);
    reduction_inputs.push_back(curr_i->args[1]);
    return;
  } else {
    if (curr_i->args[0].is_instr()) {
      reduction_inputs.push_back(curr_i->args[0]);
    }
    if (curr_i->args[1].is_instr()) {
      reduction_inputs.push_back(curr_i->args[1]);
    }
    return;
  }
}
void right_red_search(TVec<fir::ValueR> &reduction_inputs,
                      fir::ValueR curr_base, u32 exp_sub_type) {
  if (!curr_base.is_instr()) {
    reduction_inputs.clear();
    return;
  }
  auto curr_i = curr_base.as_instr();

  if (curr_i->instr_type != fir::InstrType::BinaryInstr ||
      curr_i->subtype != exp_sub_type) {
    reduction_inputs.push_back(curr_base);
    return;
  }
  if ((curr_i->args[1].is_instr() &&
       curr_i->args[1].as_instr()->instr_type == fir::InstrType::BinaryInstr &&
       curr_i->args[1].as_instr()->subtype == exp_sub_type)) {
    right_red_search(reduction_inputs, curr_i->args[1], exp_sub_type);
    reduction_inputs.push_back(curr_i->args[0]);
    return;
  } else {
    if (curr_i->args[1].is_instr()) {
      reduction_inputs.push_back(curr_i->args[1]);
    }
    if (curr_i->args[0].is_instr()) {
      reduction_inputs.push_back(curr_i->args[0]);
    }
    return;
  }
}

/*when the operation is associative we can just collect them in any order*/
// TODO: however we dont just want any order best case we would want to
// reorder them to improve chances for stuff like loads to be contininous
void red_search_assoc(TVec<fir::ValueR> &reduction_inputs,
                      fir::ValueR curr_base, u32 exp_sub_type) {
  ASSERT(curr_base.is_instr());
  auto curr_i = curr_base.as_instr();

  if (curr_i->instr_type != fir::InstrType::BinaryInstr ||
      curr_i->subtype != exp_sub_type) {
    reduction_inputs.push_back(curr_base);
    return;
  }
  if (curr_i->args[0].is_instr() || curr_i->args[1].is_instr()) {
    reduction_inputs.push_back(curr_base);
    return;
  }
  red_search_assoc(reduction_inputs, curr_i->args[0], exp_sub_type);
  red_search_assoc(reduction_inputs, curr_i->args[1], exp_sub_type);
}
}  // namespace

std::optional<SLPVectorizer::SeedBundle> SLPVectorizer::find_reduction(
    fir::BasicBlock bb, size_t instr_id, AliasAnalyis &aa) {
  SeedBundle curr;
  (void)bb;
  (void)aa;
  (void)instr_id;
  auto base_instr = bb->instructions[instr_id];
  auto subtype = base_instr->subtype;

  if (!base_instr->is(fir::InstrType::BinaryInstr)) {
    return {};
  }
  bool isProd = subtype == (u32)fir::BinaryInstrSubType::FloatMul ||
                subtype == (u32)fir::BinaryInstrSubType::IntMul;
  bool isSum = subtype == (u32)fir::BinaryInstrSubType::FloatAdd ||
               subtype == (u32)fir::BinaryInstrSubType::IntAdd;
  bool isBool = subtype == (u32)fir::BinaryInstrSubType::Or ||
                subtype == (u32)fir::BinaryInstrSubType::And ||
                subtype == (u32)fir::BinaryInstrSubType::Xor;
  bool is_associative = subtype == (u32)fir::BinaryInstrSubType::IntMul ||
                        subtype == (u32)fir::BinaryInstrSubType::IntAdd ||
                        isBool;
  if (!isProd && !isSum && !isBool) {
    return {};
  }

  // TODO: find bigger ones
  if (!base_instr->args[0].is_instr() || !base_instr->args[1].is_instr()) {
    return {};
  }
  {
    TVec<fir::ValueR> res1;
    TVec<fir::ValueR> res2;
    if (is_associative) {
      red_search_assoc(res1, fir::ValueR{base_instr}, subtype);
    } else {
      left_red_search(res1, fir::ValueR{base_instr}, subtype);
      right_red_search(res2, fir::ValueR{base_instr}, subtype);
    }
    auto max_size = std::max(res1.size(), res2.size());

    if (max_size == 2 || max_size == 4 ||
        (max_size == 8 && base_instr.get_type()->get_bitwidth() == 4)) {
      TVec<SeedInstrData> out;
      if (max_size == res1.size()) {
        for (auto r : res1) {
          out.push_back(SeedInstrData{.instr = r.as_instr(), .a = {}, .b = {}});
        }
      } else {
        for (auto r : res2) {
          out.push_back(SeedInstrData{.instr = r.as_instr(), .a = {}, .b = {}});
        }
      }
      return SeedBundle{.base = fir::ValueR{base_instr},
                        .type = base_instr.get_type(),
                        .data = out};
      // fmt::println("FOUND RED WITH SIZE {}", res.size());
    }
  }
  return {};
  // return SeedBundle{
  //     .base = fir::ValueR{base_instr},
  //     .type = base_instr.get_type(),
  //     .data = {
  //         SeedInstrData{
  //             .instr = base_instr->args[0].as_instr(), .a = {}, .b = {}},
  //         SeedInstrData{
  //             .instr = base_instr->args[1].as_instr(), .a = {}, .b = {}}}};
}

std::optional<SLPVectorizer::SeedBundle> SLPVectorizer::find_successive_stores(
    fir::BasicBlock bb, size_t instr_id, AliasAnalyis &aa) {
  SeedBundle curr;
  auto [data, base] = get_storeload_data(bb->instructions[instr_id]);
  curr.base = base;
  curr.type = bb->instructions[instr_id].get_type();
  curr.data.push_back(data);

  // fmt::print("===============Col store================\n{}\n",
  //            bb->instructions[instr_id]);
  for (auto i = instr_id + 1; i < bb->instructions.size(); i++) {
    auto instr = bb->instructions[i];
    if (instr->is(fir::InstrType::StoreInstr) &&
        curr.type == instr.get_type() && !instr->Atomic) {
      auto [sdata, sbase] = get_storeload_data(instr);
      // fmt::println("{} =?= {} => {}\n", curr.base, sbase, curr.base ==
      // sbase);
      if (curr.base == sbase) {
        curr.data.push_back(sdata);
      } else if (aa.alias(curr.base, sbase) !=
                 AliasAnalyis::AAResult::NoAlias) {
        // fmt::println("Failed aliasing store {}\n", instr);
        break;
      }
    } else if (instr->is(fir::InstrType::LoadInstr)) {
      // this load could load one of the previous stores
      //  if that is the case we cannot simply transform it into vector code
      //  since it would need to be placed before this load
      // TODO: we could transform it but leave the store that is associated
      // to this load alive to keep it correct? (would need to check if
      // thats even worth it)
      bool might_alias = false;
      for (const auto &s : curr.data) {
        if (aa.alias(instr->args[0], s.instr->args[0]) !=
            AliasAnalyis::AAResult::NoAlias) {
          might_alias = true;
          break;
        }
      }
      if (might_alias) {
        // fmt::println("Failed aliasing load {}\n", instr);
        break;
      }
    } else if (instr->pot_modifies_mem() || instr->pot_reads_mem()) {
      // fmt::println("Failed pot aliasing operation {}\n", instr);
      // TODO if it writes we could use aliasing to still apply this
      break;
    }
  }
  if (curr.data.size() > 1) {
    return curr;
  }
  return {};
}
void SLPVectorizer::find_seeds(fir::BasicBlock bb,
                               TVec<SeedBundle> &store_bundles,
                               TVec<SeedBundle> &load_bundles,
                               TVec<SeedBundle> &reduction_bundles,
                               AliasAnalyis &aa) {
  // collect store and reduction seed bundles
  //  also collect load bundles since that simplifiies later one checking if a
  //  load is valid to vectorize
  for (size_t i = 0; i < bb->instructions.size(); i++) {
    if (bb->instructions[i]->is(fir::InstrType::StoreInstr)) {
      auto res = find_successive_stores(bb, i, aa);
      if (res) {
        store_bundles.push_back(res.value());
      }
    } else if (bb->instructions[i]->is(fir::InstrType::LoadInstr)) {
      auto res = find_successive_loads(bb, i, aa);
      if (res) {
        load_bundles.push_back(res.value());
      }
    } else {
      auto res = find_reduction(bb, i, aa);
      if (res) {
        // fmt::println("FOUND");
        // fmt::println("LEN {} AT {} ", res.value().data.size(),
        //              res.value().base);
        // TODO("okak");
        reduction_bundles.push_back(res.value());
      }
    }
  }
  std::ranges::sort(store_bundles,
                    [](const SeedBundle &b1, const SeedBundle &b2) {
                      return b1.data.size() > b2.data.size();
                    });
  std::ranges::sort(load_bundles,
                    [](const SeedBundle &b1, const SeedBundle &b2) {
                      return b1.data.size() > b2.data.size();
                    });
  std::ranges::sort(reduction_bundles,
                    [](const SeedBundle &b1, const SeedBundle &b2) {
                      return b1.data.size() > b2.data.size();
                    });
  // fmt::println("I {}  {}", store_bundles.size(), load_bundles.size());
  // CLEANUP
  for (auto bi = load_bundles.size(); bi > 0; bi--) {
    auto &b = load_bundles[bi - 1];
    {
      // sort the loads
      std::ranges::sort(b.data, [](const auto &a, const auto &b) {
        i128 av = 0;
        i128 bv = 0;
        if (a.b.is_constant() && a.b.as_constant()->is_int()) {
          av = a.b.as_constant()->as_int();
        }
        if (b.b.is_constant() && b.b.as_constant()->is_int()) {
          bv = b.b.as_constant()->as_int();
        }
        return av < bv;
      });
    }
  }
  // fmt::println("F1 {}  {}", store_bundles.size(), load_bundles.size());

  for (auto bi = reduction_bundles.size(); bi > 0; bi--) {
    auto &b = reduction_bundles[bi - 1];
    {
      auto n_load = b.data.size();
      if (n_load != 2 && n_load != 4 && n_load != 8 && n_load != 16 &&
          n_load != 32) {
        if (n_load > 32) {
          b.data.resize(32);
        } else if (n_load > 16) {
          b.data.resize(16);
        } else if (n_load > 8) {
          b.data.resize(8);
        } else if (n_load > 4) {
          b.data.resize(4);
        } else if (n_load > 2) {
          b.data.resize(2);
        } else {
          reduction_bundles.erase(reduction_bundles.begin() + bi - 1);
          continue;
        }
      }
      if (b.type->get_size() == 1 &&
          (n_load % 8 != 0 && n_load % 16 != 0 && n_load % 32 != 0)) {
        reduction_bundles.erase(reduction_bundles.begin() + bi - 1);
        continue;
      }
      if (b.type->get_size() == 2 && (n_load % 8 != 0 && n_load % 16 != 0)) {
        reduction_bundles.erase(reduction_bundles.begin() + bi - 1);
        continue;
      }
      if (b.type->get_size() == 4 && (n_load % 4 != 0 && n_load % 8 != 0)) {
        reduction_bundles.erase(reduction_bundles.begin() + bi - 1);
        continue;
      }
      if (b.type->get_size() == 8 && (n_load % 2 != 0 && n_load % 4 != 0)) {
        reduction_bundles.erase(reduction_bundles.begin() + bi - 1);
        continue;
      }
      // sort the loads
      std::ranges::sort(b.data, [](const auto &a, const auto &b) {
        i128 av = 0;
        i128 bv = 0;
        if (a.b.is_constant() && a.b.as_constant()->is_int()) {
          av = a.b.as_constant()->as_int();
        }
        if (b.b.is_constant() && b.b.as_constant()->is_int()) {
          bv = b.b.as_constant()->as_int();
        }
        return av < bv;
      });
    }
  }

  for (auto bi = store_bundles.size(); bi > 0; bi--) {
    auto &b = store_bundles[bi - 1];
    {
      auto n_stor = b.data.size();
      if (n_stor != 2 && n_stor != 4 && n_stor != 8 && n_stor != 16 &&
          n_stor != 32) {
        // just cut it off 4head
        if (n_stor > 32) {
          b.data.resize(32);
        } else if (n_stor > 16) {
          b.data.resize(16);
        } else if (n_stor > 8) {
          b.data.resize(8);
        } else if (n_stor > 4) {
          b.data.resize(4);
        } else if (n_stor > 2) {
          b.data.resize(2);
        } else {
          store_bundles.erase(store_bundles.begin() + bi - 1);
          continue;
        }
      }
      auto overall_width = n_stor * b.type->get_bitwidth();
      while (overall_width > 512 ||
             (!utils::enable_avx512f && overall_width > 256)) {
        b.data.erase(b.data.end());
        n_stor = b.data.size();
        overall_width = n_stor * b.type->get_bitwidth();
      }

      while (b.type->get_size() == 1 &&
             (n_stor % 16 != 0 && n_stor % 32 != 0)) {
        b.data.erase(b.data.end());
        n_stor = b.data.size();
        overall_width = n_stor * b.type->get_bitwidth();
      }
      while (b.type->get_size() == 2 && (n_stor % 8 != 0 && n_stor % 16 != 0)) {
        b.data.erase(b.data.end());
        n_stor = b.data.size();
        overall_width = n_stor * b.type->get_bitwidth();
      }
      while (b.type->get_size() == 4 && (n_stor % 4 != 0 && n_stor % 8 != 0)) {
        b.data.erase(b.data.end());
        n_stor = b.data.size();
        overall_width = n_stor * b.type->get_bitwidth();
      }
      while (b.type->get_size() == 8 && (n_stor % 2 != 0 && n_stor % 4 != 0)) {
        b.data.erase(b.data.end());
        n_stor = b.data.size();
        overall_width = n_stor * b.type->get_bitwidth();
      }

      if (b.type->get_size() == 1 && n_stor < 16) {
        store_bundles.erase(store_bundles.begin() + bi - 1);
        continue;
      } else if (b.type->get_size() == 2 && n_stor < 8) {
        store_bundles.erase(store_bundles.begin() + bi - 1);
        continue;
      } else if (b.type->get_size() == 4 && n_stor < 4) {
        store_bundles.erase(store_bundles.begin() + bi - 1);
        continue;
      } else if (b.type->get_size() == 8 && n_stor < 2) {
        store_bundles.erase(store_bundles.begin() + bi - 1);
        continue;
      }

      // sort the stores
      std::ranges::sort(b.data, [](const auto &a, const auto &b) {
        i128 av = 0;
        i128 bv = 0;
        if (a.b.is_constant() && a.b.as_constant()->is_int()) {
          av = a.b.as_constant()->as_int();
        }
        if (b.b.is_constant() && b.b.as_constant()->is_int()) {
          bv = b.b.as_constant()->as_int();
        }
        return av < bv;
      });
      // check if continious
      {
        TVec<i128> offsets;
        // collect constant offsets
        bool failed = false;
        for (const auto &data : b.data) {
          if (!data.a.is_invalid() ||
              (!data.b.is_invalid() &&
               (!data.b.is_constant() || !data.b.as_constant()->is_int()))) {
            failed = true;
            break;
          }
          i128 consti = 0;
          if (data.b.is_constant()) {
            consti = data.b.as_constant()->as_int();
          }
          offsets.push_back(consti);
        }
        if (failed) {
          store_bundles.erase(store_bundles.begin() + bi - 1);
          continue;
        }

        // check they are continious
        for (size_t i = 1; i < offsets.size(); i++) {
          if (offsets[i] - offsets[i - 1] != b.type->get_size()) {
            failed = true;
            break;
          }
        }
        if (failed) {
          store_bundles.erase(store_bundles.begin() + bi - 1);
          continue;
        }
      }
    }
  }

  // // TODO: improve
  // //  any store set thats a subset of another can be deleted
  // {
  //   for (size_t ip1 = store_bundles.size(); ip1 > 0; ip1--) {
  //     auto &curr_b = store_bundles[ip1 - 1];
  //     bool is_subset = false;
  //     for (size_t i = 0; i < ip1 - 1; i++) {
  //       const auto &test_b = store_bundles[i];
  //       if (curr_b.base != test_b.base || curr_b.type != test_b.type) {
  //         continue;
  //       }
  //       is_subset = true;
  //       break;
  //     }
  //     if (is_subset) {
  //       store_bundles.erase(store_bundles.begin() + ip1 - 1);
  //     }
  //   }
  // }
}
bool SLPVectorizer::constant_vector_store(fir::Context &ctx,
                                          SeedBundle &bundle) {
  IRVec<fir::ConstantValueR> args;
  for (const auto &data : bundle.data) {
    if (!data.instr->args[1].is_constant()) {
      return false;
    }
  }
  for (const auto &b : bundle.data) {
    args.push_back(b.instr->args[1].as_constant());
  }
  auto constant_data = ctx->get_constant_value(
      std::move(args), ctx->get_vec_type(bundle.type, bundle.data.size()));
  continious_vector_store(bundle, fir::ValueR{constant_data});
  return true;
}

}  // namespace foptim::optim
