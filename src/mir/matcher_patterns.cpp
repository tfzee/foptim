#include "matcher_patterns.hpp"

namespace foptim::fmir {

bool is_reg(fir::ValueR val) {
  if (val.is_constant()) {
    auto consti = val.as_constant();
    if (consti->is_int()) {
      return false;
    }
    if (consti->is_global()) {
      return true;
    }
  } else {
    return true;
  }
  ASSERT(false);
  std::abort();
}

void memory_patterns(IRVec<Pattern> &pats) {

  using Node = Pattern::Node;
  using InstrType = fir::InstrType;
  using NodeType = Pattern::NodeType;

  auto IntAddNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                         (u32)fir::BinaryInstrSubType::IntAdd};
  auto IntMulNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                         (u32)fir::BinaryInstrSubType::IntMul};
  auto StoreNode = Node{NodeType::Instr, InstrType::StoreInstr, 0};
  auto LoadNode = Node{NodeType::Instr, InstrType::LoadInstr, 0};

  pats.push_back(Pattern{
      {IntMulNode, IntAddNode, LoadNode},
      {{0, 1, 1}, {1, 2, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        auto mul_instr = res.matched_instrs[0];
        auto add_instr = res.matched_instrs[1];
        auto load_instr = res.matched_instrs[2];
        auto load_ty = convert_type(load_instr.get_type());
        auto add_ty_size = get_size(convert_type(add_instr.get_type()));
        if (add_ty_size != 8 && add_ty_size != 4 && add_ty_size != 2) {
          return false;
        }
        if (!is_reg(add_instr->args[0]) || !is_reg(mul_instr->args[0]) ||
            !mul_instr->args[1].is_constant()) {
          return false;
        }
        auto consti = mul_instr->args[1].as_constant();
        if (!consti->is_int()) {
          return false;
        }
        auto consti_val = consti->as_int();

        switch (consti_val) {
        default: {
          return false;
        }
        case 1:
          consti_val = 0;
          break;
        case 2:
          consti_val = 1;
          break;
        case 4:
          consti_val = 2;
          break;
        case 8:
          consti_val = 3;
          break;
        }

        // $1 = $0 * C
        // R = $2 + $1
        // where $0 and $2 must be regs and C in [1,2,4,8]
        auto res_reg =
            valueToArg(fir::ValueR(load_instr), res.result, data.alloc);
        auto base = valueToArg(add_instr->args[0], res.result, data.alloc);
        auto indx = valueToArg(mul_instr->args[0], res.result, data.alloc);
        ASSERT(base.isReg());
        ASSERT(indx.isReg());
        res.result.emplace_back(
            Opcode::mov, res_reg,
            MArgument::Mem(base.reg, indx.reg, consti_val, load_ty));
        return true;
      }});
  pats.push_back(Pattern{
      {IntMulNode, IntAddNode, StoreNode},
      {{0, 1, 1}, {1, 2, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        auto mul_instr = res.matched_instrs[0];
        auto add_instr = res.matched_instrs[1];
        auto store_instr = res.matched_instrs[2];
        auto store_ty = convert_type(store_instr.get_type());
        auto add_ty_size = get_size(convert_type(add_instr.get_type()));
        if (add_ty_size != 8 && add_ty_size != 4 && add_ty_size != 2) {
          return false;
        }
        if (!is_reg(add_instr->args[0]) || !is_reg(mul_instr->args[0]) ||
            !mul_instr->args[1].is_constant()) {
          return false;
        }
        auto consti = mul_instr->args[1].as_constant();
        if (!consti->is_int()) {
          return false;
        }
        auto consti_val = consti->as_int();

        switch (consti_val) {
        default: {
          return false;
        }
        case 1:
          consti_val = 0;
          break;
        case 2:
          consti_val = 1;
          break;
        case 4:
          consti_val = 2;
          break;
        case 8:
          consti_val = 3;
          break;
        }

        // $1 = $0 * C
        // R = $2 + $1
        // where $0 and $2 must be regs and C in [1,2,4,8]
        auto value = valueToArg(store_instr->args[1], res.result, data.alloc);
        auto base = valueToArg(add_instr->args[0], res.result, data.alloc);
        auto indx = valueToArg(mul_instr->args[0], res.result, data.alloc);
        ASSERT(base.isReg());
        ASSERT(indx.isReg());
        res.result.emplace_back(
            Opcode::mov,
            MArgument::Mem(base.reg, indx.reg, consti_val, store_ty), value);
        return true;
      }});
  pats.push_back(Pattern{
      {IntAddNode, LoadNode},
      {{0, 1, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        ASSERT(res.matched_instrs.size() == 2);
        ASSERT(res.matched_instrs[0].is_valid());
        ASSERT(res.matched_instrs[1].is_valid());

        auto add_instr = res.matched_instrs[0];
        auto load_instr = res.matched_instrs[1];

        auto res_reg =
            valueToArg(fir::ValueR(load_instr), res.result, data.alloc);
        auto load_ty = convert_type(load_instr.get_type());

        auto a0 = valueToArg(add_instr->args[0], res.result, data.alloc);

        if (add_instr->args[1].is_constant() && a0.isImm()) {
          auto c1 = add_instr->args[1].as_constant();
          if (c1->is_global()) {
            auto a1 =
                valueToArgPtr(add_instr->args[1], Type::Int64, data.alloc);
            ASSERT(a1.type == MArgument::ArgumentType::MemLabel);
            res.result.emplace_back(Opcode::mov, res_reg,
                                    MArgument::Mem(a1.label, a0.imm, load_ty));
            return true;
          }
        }

        auto a1 = valueToArg(add_instr->args[1], res.result, data.alloc);
        if (a0.isReg() && a1.isImm()) {
          res.result.emplace_back(Opcode::mov, res_reg,
                                  MArgument::Mem(a0.reg, a1.imm, load_ty));
        } else if (a0.isImm() && a1.isReg()) {
          res.result.emplace_back(Opcode::mov, res_reg,
                                  MArgument::Mem(a1.reg, a0.imm, load_ty));
        } else if (a0.isReg() && a1.isReg()) {
          res.result.emplace_back(Opcode::mov, res_reg,
                                  MArgument::Mem(a0.reg, a1.reg, load_ty));
        } else {
          // utils::Debug << "FAILED TO MATCH IT " << add_instr << "\n";
          return false;
        }
        return true;
      }});
  pats.push_back(Pattern{
      {IntAddNode, StoreNode},
      {{0, 1, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        ASSERT(res.matched_instrs.size() == 2);
        // utils::Debug << "MATCHED\n"
        //              << res.matched_instrs[0] << "\n"
        //              << res.matched_instrs[1] << "\n";
        ASSERT(res.matched_instrs[0].is_valid());
        ASSERT(res.matched_instrs[1].is_valid());
        // TODO("impl add+load pattern");

        // return false;
        auto add_instr = res.matched_instrs[0];
        auto store_instr = res.matched_instrs[1];

        auto store_ty = convert_type(store_instr.get_type());

        auto a0 = valueToArg(add_instr->args[0], res.result, data.alloc);
        auto a1 = valueToArg(add_instr->args[1], res.result, data.alloc);
        auto value = valueToArg(store_instr->args[1], res.result, data.alloc);

        if (a0.isReg() && a1.isImm()) {
          res.result.emplace_back(
              Opcode::mov, MArgument::Mem(a0.reg, a1.imm, store_ty), value);
        } else if (a0.isImm() && a1.isReg()) {
          res.result.emplace_back(
              Opcode::mov, MArgument::Mem(a1.reg, a0.imm, store_ty), value);
        } else if (a0.isReg() && a1.isReg()) {
          res.result.emplace_back(
              Opcode::mov, MArgument::Mem(a0.reg, a1.reg, store_ty), value);
        } else {
          // utils::Debug << "FAILED TO MATCH IT " << add_instr << "\n";
          return false;
        }
        return true;
      }});
  pats.push_back(Pattern{
      {LoadNode, IntAddNode},
      {{0, 1, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        auto load_instr = res.matched_instrs[0];
        auto add_instr = res.matched_instrs[1];
        auto res_reg =
            valueToArg(fir::ValueR(add_instr), res.result, data.alloc);
        auto a0 =
            valueToArgPtr(load_instr->args[0],
                          convert_type(load_instr.get_type()), data.alloc);
        a0.ty = convert_type(load_instr.get_type());
        auto a1 = valueToArg(add_instr->args[1], res.result, data.alloc);
        res.result.emplace_back(Opcode::add, res_reg, a0, a1);
        return true;
      }});
  pats.push_back(Pattern{
      {IntMulNode, IntAddNode},
      {{0, 1, 1}},
      [](MatchResult &res, ExtraMatchData &data) {
        auto mul_instr = res.matched_instrs[0];
        auto add_instr = res.matched_instrs[1];
        auto res_ty = convert_type(add_instr.get_type());
        auto res_ty_size = get_size(res_ty);
        if (res_ty_size != 8 && res_ty_size != 4 && res_ty_size != 2) {
          return false;
        }
        if (!is_reg(add_instr->args[0]) || !is_reg(mul_instr->args[0]) ||
            !mul_instr->args[1].is_constant()) {
          return false;
        }
        auto consti = mul_instr->args[1].as_constant();
        if (!consti->is_int()) {
          return false;
        }
        auto consti_val = consti->as_int();

        switch (consti_val) {
        default: {
          return false;
        }
        case 1:
          consti_val = 0;
          break;
        case 2:
          consti_val = 1;
          break;
        case 4:
          consti_val = 2;
          break;
        case 8:
          consti_val = 3;
          break;
        }

        // $1 = $0 * C
        // R = $2 + $1
        // where $0 and $2 must be regs and C in [1,2,4,8]
        auto res_reg =
            valueToArg(fir::ValueR(add_instr), res.result, data.alloc);
        auto base = valueToArg(add_instr->args[0], res.result, data.alloc);
        auto indx = valueToArg(mul_instr->args[0], res.result, data.alloc);
        ASSERT(base.isReg());
        ASSERT(indx.isReg());
        res.result.emplace_back(
            Opcode::lea, res_reg,
            MArgument::Mem(base.reg, indx.reg, consti_val, res_ty));
        return true;
      }});
}

void cjmp_patterns(IRVec<Pattern> &pats) {
  using Node = Pattern::Node;
  using InstrType = fir::InstrType;
  using NodeType = Pattern::NodeType;
  auto ICMPNode = Node{NodeType::Instr, InstrType::ICmp, 0};
  auto FCMPNode = Node{NodeType::Instr, InstrType::FCmp, 0};
  auto CondBranchNode = Node{NodeType::Instr, InstrType::CondBranchInstr, 0};

  pats.push_back(Pattern{
      {ICMPNode, CondBranchNode},
      {{0, 1, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        // utils::Debug << "WE REACHED HERE"
        //              << " +  branch\n";
        auto cmp_instr = res.matched_instrs[0];
        auto branch_instr = res.matched_instrs[1];

        auto sub_type = (fir::ICmpInstrSubType)cmp_instr->get_instr_subtype();

        // first we check if we can output a simplified version
        if (sub_type != fir::ICmpInstrSubType::SLT &&
            sub_type != fir::ICmpInstrSubType::SGE &&
            sub_type != fir::ICmpInstrSubType::SLE &&
            sub_type != fir::ICmpInstrSubType::SGT &&
            sub_type != fir::ICmpInstrSubType::NE &&
            sub_type != fir::ICmpInstrSubType::EQ &&
            sub_type != fir::ICmpInstrSubType::ULT &&
            sub_type != fir::ICmpInstrSubType::ULE &&
            sub_type != fir::ICmpInstrSubType::UGT &&
            sub_type != fir::ICmpInstrSubType::UGE) {
          utils::Debug << "Failed to smartly match cmp "
                       << cmp_instr->get_instr_subtype() << " +  branch\n";
          return false;
        }

        if (!branch_instr->bbs[0].args.empty()) {
          // if we have bb args that collide with our conditional stuff we skip
          // it cause it might be overriding values
          // TOOD: IMPL checking if theres actually a collision
          // current issue is that to know where we store the args to the
          // comparison we need to generate the args
          //   if we then fail the check we will have generated dead code
          // for (auto arg : branch_instr->bbs[0].args) {
          //   auto bb_arg_target = valueToArg(fir::ValueR(arg), res.result,
          //                        data.alloc);
          //   ASSERT(bb_arg_target.isReg());
          //   if (bb_arg_target == ){

          //   }
          // }
          utils::Debug << "Failed to smartly match cmp "
                       << cmp_instr->get_instr_subtype()
                       << " + branch because non emtpy bb args\n";
          return false;
        }

        auto bb_with_args = branch_instr->bbs[0];
        auto target_bb = branch_instr->bbs[0].bb;
        auto v1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
        auto v2 = valueToArg(cmp_instr->args[1], res.result, data.alloc);

        // auto comp_ty = v1_orig.ty;

        // auto v1_reg = data.alloc.get_new_register(VRegInfo{comp_ty});
        // auto v2_reg = data.alloc.get_new_register(VRegInfo{comp_ty});
        // auto v1 = MArgument{v1_reg, comp_ty};
        // auto v2 = MArgument{v2_reg, comp_ty};

        // TODO: this is a issue with lifetimes
        // res.result.emplace_back(Opcode::mov, v1, v1_orig);
        // res.result.emplace_back(Opcode::mov, v2, v2_orig);

        ASSERT(bb_with_args.args.size() == target_bb->args.size());
        ASSERT(bb_with_args.args.size() == 0);
        generate_bb_args(bb_with_args, res, data);

        if (sub_type == fir::ICmpInstrSubType::SLT) {
          res.result.push_back(
              MInstr::cJmp_slt(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::SGT) {
          res.result.push_back(
              MInstr::cJmp_sgt(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::ULT) {
          res.result.push_back(
              MInstr::cJmp_ult(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::ULE) {
          res.result.push_back(
              MInstr::cJmp_ule(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::UGT) {
          res.result.push_back(
              MInstr::cJmp_ugt(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::UGE) {
          res.result.push_back(
              MInstr::cJmp_uge(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::SGE) {
          res.result.push_back(
              MInstr::cJmp_sge(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::SLE) {
          res.result.push_back(
              MInstr::cJmp_sle(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::EQ) {
          res.result.push_back(
              MInstr::cJmp_eq(v1, v2, data.bbs[bb_with_args.bb]));
        } else if (sub_type == fir::ICmpInstrSubType::NE) {
          res.result.push_back(
              MInstr::cJmp_ne(v1, v2, data.bbs[bb_with_args.bb]));
        } else {
          UNREACH();
        }

        {
          auto bb2_with_args = branch_instr->bbs[1];
          auto target_bb2 = branch_instr->bbs[1].bb;
          ASSERT(bb2_with_args.args.size() == target_bb2->args.size());
          // ASSERT(bb2_with_args.args.size() == 0);
          generate_bb_args(bb2_with_args, res, data);
          res.result.push_back(MInstr::jmp(data.bbs[bb2_with_args.bb]));
        }
        return true;
      }});
  pats.push_back(Pattern{
      {FCMPNode, CondBranchNode},
      {{0, 1, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        // utils::Debug << "WE REACHED HERE"
        //              << " +  branch\n";
        auto cmp_instr = res.matched_instrs[0];
        auto branch_instr = res.matched_instrs[1];

        auto sub_type = (fir::FCmpInstrSubType)cmp_instr->get_instr_subtype();

        auto bb_with_args = branch_instr->bbs[0];
        auto target_bb = branch_instr->bbs[0].bb;
        auto v1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
        auto v2 = valueToArg(cmp_instr->args[1], res.result, data.alloc);

        ASSERT(bb_with_args.args.size() == target_bb->args.size());
        generate_bb_args(bb_with_args, res, data);

        res.result.push_back(
            MInstr::cJmp_flt(v1, v2, data.bbs[bb_with_args.bb], sub_type));

        {
          auto bb2_with_args = branch_instr->bbs[1];
          auto target_bb2 = branch_instr->bbs[1].bb;
          ASSERT(bb2_with_args.args.size() == target_bb2->args.size());
          generate_bb_args(bb2_with_args, res, data);
          res.result.push_back(MInstr::jmp(data.bbs[bb2_with_args.bb]));
        }
        return true;
      }});
}
void arith_patterns(IRVec<Pattern> &pats) {
  using Node = Pattern::Node;
  using InstrType = fir::InstrType;
  using NodeType = Pattern::NodeType;

  // auto IntAddNode = Node{NodeType::Instr, InstrType::BinaryInstr,
  //                        (u32)fir::BinaryInstrSubType::IntAdd};
  // auto IntSubNode = Node{NodeType::Instr, InstrType::BinaryInstr,
  //                        (u32)fir::BinaryInstrSubType::IntSub};
  // auto IntMulNode = Node{NodeType::Instr, InstrType::BinaryInstr,
  //                        (u32)fir::BinaryInstrSubType::IntMul};
  // auto SRemNode = Node{NodeType::Instr, InstrType::BinaryInstr,
  //                      (u32)fir::BinaryInstrSubType::IntSRem};
  // auto SDivNode = Node{NodeType::Instr, InstrType::BinaryInstr,
  //                      (u32)fir::BinaryInstrSubType::IntSDiv};
  // auto AndNode = Node{NodeType::Instr, InstrType::BinaryInstr,
  //                     (u32)fir::BinaryInstrSubType::And};
  // auto OrNode = Node{NodeType::Instr, InstrType::BinaryInstr,
  //                    (u32)fir::BinaryInstrSubType::Or};
  // auto XorNode = Node{NodeType::Instr, InstrType::BinaryInstr,
  //                     (u32)fir::BinaryInstrSubType::Xor};
  auto FloatAddNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                           (u32)fir::BinaryInstrSubType::FloatAdd};
  auto FloatMulNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                           (u32)fir::BinaryInstrSubType::FloatMul};
  // auto ShlNode = Node{NodeType::Instr, InstrType::BinaryInstr,
  //                     (u32)fir::BinaryInstrSubType::Shl};
  // auto ShrNode = Node{NodeType::Instr, InstrType::BinaryInstr,
  //                     (u32)fir::BinaryInstrSubType::Shr};
  // auto AShrNode = Node{NodeType::Instr, InstrType::BinaryInstr,
  //                      (u32)fir::BinaryInstrSubType::AShr};
  // auto ConversionNode = Node{NodeType::Instr, InstrType::Conversion, 0};
  pats.push_back(Pattern{
      {FloatMulNode, FloatAddNode},
      {{0, 1, 0}},
      [](MatchResult &res, ExtraMatchData &data) {
        return false;
        auto mul_instr = res.matched_instrs[0];
        auto add_instr = res.matched_instrs[1];
        auto res_reg =
            valueToArg(fir::ValueR(add_instr), res.result, data.alloc);
        auto mul_arg1 = valueToArg(mul_instr->args[0], res.result, data.alloc);
        auto mul_arg2 = valueToArg(mul_instr->args[1], res.result, data.alloc);
        auto add_arg2 = valueToArg(mul_instr->args[1], res.result, data.alloc);
        // utils::Debug << "MATHCING FMADD\n";
        // utils::Debug << mul_instr << "\n";
        // utils::Debug << add_instr << "\n";
        // utils::Debug << mul_arg1 << " * " << mul_arg2 << " + " << add_arg2
        //              << "\n";

        if (res_reg == add_arg2 && add_arg2.isReg() && mul_arg1.isReg() &&
            (mul_arg2.isReg() || mul_arg2.isMem())) {
          res.result.emplace_back(Opcode::ffmadd231, res_reg, mul_arg1,
                                  mul_arg2);
        } else if (res_reg == mul_arg1 && mul_arg1.isReg() &&
                   mul_arg2.isReg() && (add_arg2.isReg() || add_arg2.isMem())) {
          res.result.emplace_back(Opcode::ffmadd231, mul_arg1, mul_arg2,

                                  add_arg2);
        } else if (res_reg == mul_arg2 && mul_arg1.isReg() &&
                   mul_arg2.isReg() && (add_arg2.isReg() || add_arg2.isMem())) {
          res.result.emplace_back(Opcode::ffmadd231, mul_arg2, mul_arg1,

                                  add_arg2);
        } else if (res_reg == mul_arg1 && mul_arg1.isReg() &&
                   add_arg2.isReg() && (mul_arg2.isReg() || mul_arg2.isMem())) {
          res.result.emplace_back(Opcode::ffmadd132, mul_arg1, add_arg2,
                                  mul_arg2);
        } else if (res_reg == mul_arg2 && mul_arg2.isReg() &&
                   add_arg2.isReg() && (mul_arg1.isReg() || mul_arg1.isMem())) {
          res.result.emplace_back(Opcode::ffmadd132, mul_arg2, add_arg2,
                                  mul_arg1);
        // } else if (add_arg2.isReg()) {
        //   // TODO: could be expanded to handle another memory operand in the
        //   // move explicitly flipping arround the mul
        //   res.result.emplace_back(Opcode::mov, res_reg, mul_arg1);
        //   res.result.emplace_back(Opcode::ffmadd132, res_reg, add_arg2,
        //                           mul_arg2);
        // } else if (mul_arg2.isReg()) {
        //   res.result.emplace_back(Opcode::mov, res_reg, mul_arg1);
        //   res.result.emplace_back(Opcode::ffmadd213, res_reg, mul_arg2,
        //                           add_arg2);
        } else {
          // TODO: prob shouldnt do this and just let the legalizer handle it
          res.result.emplace_back(Opcode::fmul, res_reg, mul_arg1, mul_arg2);
          res.result.emplace_back(Opcode::fadd, res_reg, res_reg, add_arg2);
        }
        utils::Debug << res.result.back() << "\n";
        return true;
      }});
}

void base_patterns(IRVec<Pattern> &pats) {
  using Node = Pattern::Node;
  using InstrType = fir::InstrType;
  using NodeType = Pattern::NodeType;

  auto IntAddNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                         (u32)fir::BinaryInstrSubType::IntAdd};
  auto IntSubNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                         (u32)fir::BinaryInstrSubType::IntSub};
  auto IntMulNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                         (u32)fir::BinaryInstrSubType::IntMul};
  auto SRemNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                       (u32)fir::BinaryInstrSubType::IntSRem};
  auto SDivNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                       (u32)fir::BinaryInstrSubType::IntSDiv};
  auto AndNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                      (u32)fir::BinaryInstrSubType::And};
  auto OrNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                     (u32)fir::BinaryInstrSubType::Or};
  auto XorNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                      (u32)fir::BinaryInstrSubType::Xor};
  auto FloatAddNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                           (u32)fir::BinaryInstrSubType::FloatAdd};
  auto FloatSubNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                           (u32)fir::BinaryInstrSubType::FloatSub};
  auto FloatMulNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                           (u32)fir::BinaryInstrSubType::FloatMul};
  auto ShlNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                      (u32)fir::BinaryInstrSubType::Shl};
  auto ShrNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                      (u32)fir::BinaryInstrSubType::Shr};
  auto AShrNode = Node{NodeType::Instr, InstrType::BinaryInstr,
                       (u32)fir::BinaryInstrSubType::AShr};
  auto ConversionNode = Node{NodeType::Instr, InstrType::Conversion, 0};
  // auto EQNode =
  //     Node{NodeType::Instr, InstrType::ICmp, (u32)fir::ICmpInstrSubType::EQ};
  // auto SLTNode =
  //     Node{NodeType::Instr, InstrType::ICmp,
  //     (u32)fir::ICmpInstrSubType::SLT};
  auto ICMPNode = Node{NodeType::Instr, InstrType::ICmp, 0};
  // auto FCMPNode = Node{NodeType::Instr, InstrType::FCmp, 0};
  auto BranchNode = Node{NodeType::Instr, InstrType::BranchInstr, 0};
  auto CondBranchNode = Node{NodeType::Instr, InstrType::CondBranchInstr, 0};
  auto ReturnNode = Node{NodeType::Instr, InstrType::ReturnInstr, 0};
  auto CallNode = Node{NodeType::Instr, InstrType::CallInstr, 0};
  auto StoreNode = Node{NodeType::Instr, InstrType::StoreInstr, 0};
  auto LoadNode = Node{NodeType::Instr, InstrType::LoadInstr, 0};
  auto AllocaNode = Node{NodeType::Instr, InstrType::AllocaInstr, 0};
  auto SExtNode = Node{NodeType::Instr, InstrType::SExt, 0};
  auto ZExtNode = Node{NodeType::Instr, InstrType::ZExt, 0};
  auto ITruncNode = Node{NodeType::Instr, InstrType::ITrunc, 0};
  auto SelectNode = Node{NodeType::Instr, InstrType::SelectInstr, 0};

  pats.push_back(
      Pattern{{AllocaNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                // TODO: this should be done once for all allocas that only get
                // executed once
                auto alloca_instr = res.matched_instrs[0];
                auto rsp_reg = data.alloc.get_new_register(VRegInfo::RSP());
                auto rsp_arg = MArgument{rsp_reg, Type::Int64};

                auto res_reg = valueToArg(fir::ValueR(alloca_instr), res.result,
                                          data.alloc);

                auto size = alloca_instr->args[0].as_constant()->as_int();

                res.result.emplace_back(Opcode::sub, rsp_arg, rsp_arg, size);
                res.result.emplace_back(Opcode::mov, res_reg, rsp_arg);
                return true;
              }});
  pats.push_back(
      Pattern{{LoadNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto load_instr = res.matched_instrs[0];
                auto res_reg =
                    valueToArg(fir::ValueR(load_instr), res.result, data.alloc);
                auto arg = valueToArgPtr(load_instr->args[0],
                                         convert_type(load_instr.get_type()),
                                         data.alloc);
                arg.ty = convert_type(load_instr.get_type());
                res.result.emplace_back(Opcode::mov, res_reg, arg);
                return true;
              }});
  pats.push_back(Pattern{
      {StoreNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto store_instr = res.matched_instrs[0];
        auto value = valueToArg(store_instr->args[1], res.result, data.alloc);

        auto ptr_target =
            valueToArgPtr(store_instr->args[0],
                          convert_type(store_instr.get_type()), data.alloc);
        ptr_target.ty = convert_type(store_instr.get_type());
        res.result.emplace_back(Opcode::mov, ptr_target, value);
        return true;
      }});
  pats.push_back(Pattern{
      {IntAddNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto add_instr = res.matched_instrs[0];
        auto res_reg =
            valueToArg(fir::ValueR(add_instr), res.result, data.alloc);
        auto a0 = valueToArg(add_instr->args[0], res.result, data.alloc);

        auto res_ty = convert_type(add_instr.get_type());

        if (res_reg.ty != a0.ty) {
          auto res_reg = data.alloc.get_new_register(VRegInfo{res_ty});
          auto helper_reg0 = MArgument(res_reg, res_ty);

          res.result.emplace_back(Opcode::mov, helper_reg0, a0);
          a0 = helper_reg0;
        }

        auto a1 = valueToArg(add_instr->args[1], res.result, data.alloc);
        if (a1.isImm()) {
          // then we gucci
        } else if (res_reg.ty != a1.ty) {
          auto res_reg = data.alloc.get_new_register(VRegInfo{res_ty});
          auto helper_reg1 = MArgument(res_reg, res_ty);

          res.result.emplace_back(Opcode::mov, helper_reg1, a1);
          a1 = helper_reg1;
        }

        res.result.emplace_back(Opcode::add, res_reg, a0, a1);
        return true;
      }});
  pats.push_back(Pattern{
      {SelectNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto select_instr = res.matched_instrs[0];
        auto res_reg =
            valueToArg(fir::ValueR(select_instr), res.result, data.alloc);
        auto cond = valueToArg(select_instr->args[0], res.result, data.alloc);
        auto a = valueToArg(select_instr->args[1], res.result, data.alloc);
        auto b = valueToArg(select_instr->args[2], res.result, data.alloc);

        res.result.emplace_back(Opcode::mov, res_reg, a);
        res.result.emplace_back(Opcode::cmov, res_reg, cond, b);
        return true;
      }});
  pats.push_back(
      Pattern{{IntSubNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto sub_instr = res.matched_instrs[0];
                auto res_reg =
                    valueToArg(fir::ValueR(sub_instr), res.result, data.alloc);

                res.result.emplace_back(
                    Opcode::sub, res_reg,
                    valueToArg(sub_instr->args[0], res.result, data.alloc),
                    valueToArg(sub_instr->args[1], res.result, data.alloc));
                return true;
              }});
  pats.push_back(Pattern{
      {ShlNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto shift_instr = res.matched_instrs[0];
        auto res_reg =
            valueToArg(fir::ValueR(shift_instr), res.result, data.alloc);

        auto a = valueToArg(shift_instr->args[0], res.result, data.alloc);
        auto b = valueToArg(shift_instr->args[1], res.result, data.alloc);

        if (b.isImm()) {
          res.result.emplace_back(Opcode::shl, res_reg, a, b);
        } else {
          auto shift_reg =
              data.alloc.get_new_pinned_register({shift_instr}, VRegInfo::CL());
          auto shift_reg_arg = MArgument(shift_reg, Type::Int8);
          if (b.ty == Type::Int8) {
            res.result.emplace_back(Opcode::mov, shift_reg_arg, b);
          } else {
            res.result.emplace_back(Opcode::itrunc, shift_reg_arg, b);
          }
          res.result.emplace_back(Opcode::shl, res_reg, a, shift_reg_arg);
        }
        return true;
      }});
  pats.push_back(Pattern{
      {ShrNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto shift_instr = res.matched_instrs[0];
        auto res_reg =
            valueToArg(fir::ValueR(shift_instr), res.result, data.alloc);

        auto a = valueToArg(shift_instr->args[0], res.result, data.alloc);
        auto b = valueToArg(shift_instr->args[1], res.result, data.alloc);

        if (b.isImm()) {
          res.result.emplace_back(Opcode::shr, res_reg, a, b);
        } else {
          auto shift_reg =
              data.alloc.get_new_pinned_register({shift_instr}, VRegInfo::CL());
          auto shift_reg_arg = MArgument(shift_reg, Type::Int8);
          if (b.ty == Type::Int8) {
            res.result.emplace_back(Opcode::mov, shift_reg_arg, b);
          } else {
            res.result.emplace_back(Opcode::itrunc, shift_reg_arg, b);
          }
          res.result.emplace_back(Opcode::shr, res_reg, a, shift_reg_arg);
        }
        return true;
      }});
  pats.push_back(Pattern{
      {AShrNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto shift_instr = res.matched_instrs[0];
        auto res_reg =
            valueToArg(fir::ValueR(shift_instr), res.result, data.alloc);

        auto a = valueToArg(shift_instr->args[0], res.result, data.alloc);
        auto b = valueToArg(shift_instr->args[1], res.result, data.alloc);

        if (b.isImm()) {
          res.result.emplace_back(Opcode::sar, res_reg, a, b);
        } else {
          auto shift_reg =
              data.alloc.get_new_pinned_register({shift_instr}, VRegInfo::CL());
          auto shift_reg_arg = MArgument(shift_reg, Type::Int8);
          if (b.ty == Type::Int8) {
            res.result.emplace_back(Opcode::mov, shift_reg_arg, b);
          } else {
            res.result.emplace_back(Opcode::itrunc, shift_reg_arg, b);
          }
          res.result.emplace_back(Opcode::sar, res_reg, a, shift_reg_arg);
        }
        return true;
      }});
  pats.push_back(
      Pattern{{IntMulNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto add_instr = res.matched_instrs[0];
                auto res_reg =
                    valueToArg(fir::ValueR(add_instr), res.result, data.alloc);

                res.result.emplace_back(
                    Opcode::mul, res_reg,
                    valueToArg(add_instr->args[0], res.result, data.alloc),
                    valueToArg(add_instr->args[1], res.result, data.alloc));
                return true;
              }});
  pats.push_back(
      Pattern{{OrNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto or_instr = res.matched_instrs[0];
                auto res_reg =
                    valueToArg(fir::ValueR(or_instr), res.result, data.alloc);

                res.result.emplace_back(
                    Opcode::lor, res_reg,
                    valueToArg(or_instr->args[0], res.result, data.alloc),
                    valueToArg(or_instr->args[1], res.result, data.alloc));
                return true;
              }});
  pats.push_back(
      Pattern{{AndNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto and_instr = res.matched_instrs[0];
                auto res_reg =
                    valueToArg(fir::ValueR(and_instr), res.result, data.alloc);

                res.result.emplace_back(
                    Opcode::land, res_reg,
                    valueToArg(and_instr->args[0], res.result, data.alloc),
                    valueToArg(and_instr->args[1], res.result, data.alloc));
                return true;
              }});
  pats.push_back(
      Pattern{{XorNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto xor_instr = res.matched_instrs[0];
                auto res_reg =
                    valueToArg(fir::ValueR(xor_instr), res.result, data.alloc);

                res.result.emplace_back(
                    Opcode::lxor, res_reg,
                    valueToArg(xor_instr->args[0], res.result, data.alloc),
                    valueToArg(xor_instr->args[1], res.result, data.alloc));
                return true;
              }});
  pats.push_back(Pattern{
      {SRemNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto srem_instr = res.matched_instrs[0];
        // FIXME: variable size
        auto res_div = data.alloc.get_new_register(fir::IRLocation{srem_instr},
                                                   srem_instr.get_type(),
                                                   VRegInfo::EAX(), data.lives);
        auto res_rem = data.alloc.get_new_register(fir::IRLocation{srem_instr},
                                                   srem_instr.get_type(),
                                                   VRegInfo::EDX(), data.lives);
        auto res_reg =
            valueToArg(fir::ValueR(srem_instr), res.result, data.alloc);
        auto res_div_arg =
            MArgument(res_div, convert_type(srem_instr.get_type()));
        auto res_rem_arg =
            MArgument(res_rem, convert_type(srem_instr.get_type()));

        res.result.emplace_back(
            Opcode::idiv, res_div_arg, res_rem_arg,
            valueToArg(srem_instr->args[0], res.result, data.alloc),
            valueToArg(srem_instr->args[1], res.result, data.alloc));
        res.result.emplace_back(Opcode::mov, res_reg, res_rem_arg);
        return true;
      }});
  pats.push_back(Pattern{
      {SDivNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto sdiv_instr = res.matched_instrs[0];
        // FIXME: variable size
        auto res_div = data.alloc.get_new_register(fir::IRLocation{sdiv_instr},
                                                   sdiv_instr.get_type(),
                                                   VRegInfo::EAX(), data.lives);
        auto res_rem = data.alloc.get_new_register(fir::IRLocation{sdiv_instr},
                                                   sdiv_instr.get_type(),
                                                   VRegInfo::EDX(), data.lives);
        auto res_reg =
            valueToArg(fir::ValueR(sdiv_instr), res.result, data.alloc);
        auto res_div_arg =
            MArgument(res_div, convert_type(sdiv_instr.get_type()));
        auto res_rem_arg =
            MArgument(res_rem, convert_type(sdiv_instr.get_type()));

        res.result.emplace_back(
            Opcode::idiv, res_div_arg, res_rem_arg,
            valueToArg(sdiv_instr->args[0], res.result, data.alloc),
            valueToArg(sdiv_instr->args[1], res.result, data.alloc));
        res.result.emplace_back(Opcode::mov, res_reg, res_div_arg);
        return true;
      }});
  pats.push_back(Pattern{
      {ICMPNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto cmp_instr = res.matched_instrs[0];
        auto res_reg = data.alloc.get_register(fir::ValueR(cmp_instr));

        auto res_arg = MArgument(res_reg, convert_type(cmp_instr.get_type()));

        auto arg1 = valueToArg(cmp_instr->args[0], res.result, data.alloc);
        auto arg2 = valueToArg(cmp_instr->args[1], res.result, data.alloc);

        Opcode op = Opcode::icmp_eq;

        switch ((fir::ICmpInstrSubType)cmp_instr->get_instr_subtype()) {
        case fir::ICmpInstrSubType::SLT:
          op = Opcode::icmp_slt;
          break;
        case fir::ICmpInstrSubType::ULT:
          op = Opcode::icmp_ult;
          break;
        case fir::ICmpInstrSubType::NE:
          op = Opcode::icmp_ne;
          break;
        case fir::ICmpInstrSubType::EQ:
          op = Opcode::icmp_eq;
          break;
        case fir::ICmpInstrSubType::SGT:
          op = Opcode::icmp_sgt;
          break;
        case fir::ICmpInstrSubType::UGT:
          op = Opcode::icmp_ugt;
          break;
        case fir::ICmpInstrSubType::UGE:
          op = Opcode::icmp_uge;
          break;
        case fir::ICmpInstrSubType::ULE:
          op = Opcode::icmp_ule;
          break;
        case fir::ICmpInstrSubType::SGE:
          op = Opcode::icmp_sge;
          break;
        case fir::ICmpInstrSubType::SLE:
          op = Opcode::icmp_sle;
          break;
        case fir::ICmpInstrSubType::INVALID:
          UNREACH();
        }
        res.result.emplace_back(op, res_arg, arg1, arg2);
        return true;
      }});
  pats.push_back(
      Pattern{{BranchNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
                auto branch_instr = res.matched_instrs[0];
                auto bb_with_args = branch_instr->bbs[0];
                auto target_bb = branch_instr->bbs[0].bb;
                ASSERT(bb_with_args.args.size() == target_bb->args.size());
                generate_bb_args(bb_with_args, res, data);
                res.result.push_back(MInstr::jmp(data.bbs[bb_with_args.bb]));
                return true;
              }});
  pats.push_back(Pattern{
      {CondBranchNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto branch_instr = res.matched_instrs[0];
        auto cond = valueToArg(branch_instr->args[0], res.result, data.alloc);
        {
          auto bb_with_args = branch_instr->bbs[0];
          auto target_bb = branch_instr->bbs[0].bb;
          ASSERT(bb_with_args.args.size() == target_bb->args.size());
          generate_bb_args(bb_with_args, res, data);
          res.result.push_back(MInstr::cJmp(cond, data.bbs[bb_with_args.bb]));
        }

        {
          auto bb2_with_args = branch_instr->bbs[1];
          auto target_bb2 = branch_instr->bbs[1].bb;
          ASSERT(bb2_with_args.args.size() == target_bb2->args.size());
          generate_bb_args(bb2_with_args, res, data);
          res.result.push_back(MInstr::jmp(data.bbs[bb2_with_args.bb]));
        }
        return true;
      }});
  pats.push_back(Pattern{
      {ReturnNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto ret_instr = res.matched_instrs[0];
        if (ret_instr->has_args()) {
          auto ret_val = valueToArg(ret_instr->args[0], res.result, data.alloc);
          auto is_float_val = ret_val.ty == fmir::Type::Float64 ||
                              ret_val.ty == fmir::Type::Float32;

          if (!is_float_val &&
              (!ret_val.isReg() || ret_val.reg.info.ty != VRegType::A)) {
            auto converted_type = convert_type(ret_instr.get_type());
            auto res_reg = data.alloc.get_new_register(
                fir::IRLocation{ret_instr}, ret_instr.get_type(),
                VRegInfo{VRegType::A, converted_type}, data.lives);
            auto res_arg = MArgument(res_reg, converted_type);
            res.result.emplace_back(Opcode::mov, res_arg, ret_val);
            res.result.emplace_back(Opcode::ret, res_arg);
          } else if (is_float_val && (!ret_val.isReg() ||
                                      ret_val.reg.info.ty != VRegType::mm0)) {
            auto converted_type = convert_type(ret_instr.get_type());
            auto res_reg = data.alloc.get_new_register(
                fir::IRLocation{ret_instr}, ret_instr.get_type(),
                VRegInfo{VRegType::mm0, converted_type}, data.lives);
            auto res_arg = MArgument(res_reg, converted_type);
            utils::Debug << "RETTY\n";
            res.result.emplace_back(Opcode::mov, res_arg, ret_val);
            res.result.emplace_back(Opcode::ret, res_arg);
          } else {
            res.result.emplace_back(Opcode::ret, ret_val);
          }
        } else {
          res.result.emplace_back(Opcode::ret);
        }
        return true;
      }});
  pats.push_back(Pattern{
      {SExtNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto sext_instr = res.matched_instrs[0];
        auto val = valueToArg(sext_instr->args[0], res.result, data.alloc);
        auto res_reg =
            valueToArg(fir::ValueR(sext_instr), res.result, data.alloc);

        res.result.emplace_back(Opcode::mov_sx, res_reg, val);
        return true;
      }});
  pats.push_back(Pattern{
      {ZExtNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto zext_instr = res.matched_instrs[0];
        auto val = valueToArg(zext_instr->args[0], res.result, data.alloc);
        auto res_reg =
            valueToArg(fir::ValueR(zext_instr), res.result, data.alloc);

        res.result.emplace_back(Opcode::mov_zx, res_reg, val);
        return true;
      }});
  pats.push_back(Pattern{
      {ConversionNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto conversion_instr = res.matched_instrs[0];
        auto val =
            valueToArg(conversion_instr->args[0], res.result, data.alloc);
        auto res_reg =
            valueToArg(fir::ValueR(conversion_instr), res.result, data.alloc);
        auto res_opcode = Opcode::FL2SI;
        switch ((fir::ConversionSubType)conversion_instr->subtype) {
        case fir::ConversionSubType::INVALID:
          UNREACH();
        case fir::ConversionSubType::FPTOUI:
          res_opcode = Opcode::FL2UI;
          break;
        case fir::ConversionSubType::FPTOSI:
          res_opcode = Opcode::FL2SI;
          break;
        case fir::ConversionSubType::UITOFP:
          res_opcode = Opcode::UI2FL;
          break;
        case fir::ConversionSubType::SITOFP:
          res_opcode = Opcode::SI2FL;
          break;
        }

        res.result.emplace_back(res_opcode, res_reg, val);
        return true;
      }});
  pats.push_back(Pattern{
      {ITruncNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto itrunc_instr = res.matched_instrs[0];
        auto val = valueToArg(itrunc_instr->args[0], res.result, data.alloc);
        auto res_reg =
            valueToArg(fir::ValueR(itrunc_instr), res.result, data.alloc);
        res.result.emplace_back(Opcode::itrunc, res_reg, val);
        return true;
      }});
  pats.push_back(Pattern{
      {CallNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto call_instr = res.matched_instrs[0];
        {
          TVec<MArgument> evaluated_args;
          for (size_t arg_id = 1; arg_id < call_instr->args.size(); arg_id++) {
            evaluated_args.push_back(
                valueToArg(call_instr->args[arg_id], res.result, data.alloc));
          }
          for (auto arg_value : evaluated_args) {
            res.result.emplace_back(Opcode::arg_setup, arg_value);
          }
        }

        MArgument calee;
        if (call_instr->args[0].is_constant()) {
          calee = valueToArgPtr(call_instr->args[0], Type::Int64, data.alloc);
        } else {
          calee = valueToArg(call_instr->args[0], res.result, data.alloc);
        }

        auto res_type = call_instr.get_type();
        if (res_type->is_void() || call_instr->get_n_uses() == 0) {
          res.result.emplace_back(Opcode::invoke, calee);
        } else if (res_type->is_int() || res_type->is_ptr()) {
          auto res_reg =
              valueToArg(fir::ValueR(call_instr), res.result, data.alloc);
          res.result.emplace_back(Opcode::invoke, calee, res_reg);
        } else if (res_type->is_float()) {
          auto res_reg =
              valueToArg(fir::ValueR(call_instr), res.result, data.alloc);
          res.result.emplace_back(Opcode::invoke, calee, res_reg);
        } else {
          ASSERT_M(false, "impl ret value");
        }
        // {
        //   auto rsp_reg = data.alloc.get_new_register(VRegInfo::ESP());
        //   res.result.push_back(MInstr{Opcode::add,
        //                               MArgument{rsp_reg, Type::Int64},
        //                               MArgument{rsp_reg, Type::Int64},
        //                               MArgument{call_instr->args.size() *
        //                               8}});
        // }
        return true;
      }});
  pats.push_back(Pattern{
      {FloatAddNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto f_add_instr = res.matched_instrs[0];
        auto a1 = valueToArg(f_add_instr->args[0], res.result, data.alloc);
        auto a2 = valueToArg(f_add_instr->args[1], res.result, data.alloc);
        auto res_reg =
            valueToArg(fir::ValueR(f_add_instr), res.result, data.alloc);

        res.result.emplace_back(Opcode::fadd, res_reg, a1, a2);
        return true;
      }});
  pats.push_back(Pattern{
      {FloatSubNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto f_sub_instr = res.matched_instrs[0];
        auto a1 = valueToArg(f_sub_instr->args[0], res.result, data.alloc);
        auto a2 = valueToArg(f_sub_instr->args[1], res.result, data.alloc);
        auto res_reg =
            valueToArg(fir::ValueR(f_sub_instr), res.result, data.alloc);

        res.result.emplace_back(Opcode::fsub, res_reg, a1, a2);
        return true;
      }});
  pats.push_back(Pattern{
      {FloatMulNode}, {}, [](MatchResult &res, ExtraMatchData &data) {
        auto f_mul_instr = res.matched_instrs[0];
        auto a1 = valueToArg(f_mul_instr->args[0], res.result, data.alloc);
        auto a2 = valueToArg(f_mul_instr->args[1], res.result, data.alloc);
        auto res_reg =
            valueToArg(fir::ValueR(f_mul_instr), res.result, data.alloc);

        res.result.emplace_back(Opcode::fmul, res_reg, a1, a2);
        return true;
      }});
}
} // namespace foptim::fmir
