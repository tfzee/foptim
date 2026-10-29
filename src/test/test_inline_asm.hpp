#include <gtest/gtest.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/LLVMContext.h>

#include "ir/context.hpp"
#include "llvm/inline_asm.hpp"
#include "x86_codegen/att_assembler.hpp"
#include "x86_codegen/inline_asm_emit.hpp"

namespace {
using foptim::fir::AsmOperandClass;
using foptim::fir::AsmOperandKind;

foptim::fir::AsmInfo parse_asm(const char *tmpl, const char *constraints,
                               bool sideeffect = false) {
  llvm::LLVMContext lctx;
  auto *fty = llvm::FunctionType::get(llvm::Type::getVoidTy(lctx), false);
  auto *as = llvm::InlineAsm::get(fty, tmpl, constraints, sideeffect);
  return foptim::parse_inline_asm(*as, "test");
}
}  // namespace

TEST(InlineAsm, TiedOutput) {
  auto info = parse_asm("addq $$1, $0", "=r,0,~{dirflag},~{fpsr},~{flags}");
  ASSERT_EQ(info.operands.size(), 2U);
  EXPECT_EQ(info.operands[0].kind, AsmOperandKind::Output);
  EXPECT_EQ(info.operands[0].cls, AsmOperandClass::Reg);
  EXPECT_EQ(info.operands[0].tied, 1);
  EXPECT_EQ(info.operands[1].kind, AsmOperandKind::Input);
  EXPECT_EQ(info.operands[1].arg, 0);
  EXPECT_FALSE(info.mem_clobber);
  EXPECT_FALSE(info.sideeffect);
}

TEST(InlineAsm, FixedRegistersAndClobbers) {
  auto info =
      parse_asm("cpuid", "={ax},={bx},={cx},={dx},0,2,~{memory},~{rsi}", true);
  EXPECT_TRUE(info.sideeffect);
  EXPECT_TRUE(info.mem_clobber);
  ASSERT_EQ(info.operands.size(), 7U);
  EXPECT_EQ(info.operands[0].cls, AsmOperandClass::Fixed);
  EXPECT_EQ(info.operands[0].reg, "ax");
  EXPECT_EQ(info.operands[3].reg, "dx");
  EXPECT_EQ(info.operands[6].kind, AsmOperandKind::Clobber);
  EXPECT_EQ(info.operands[6].reg, "rsi");
}

TEST(InlineAsm, MemoryAndEarlyClobber) {
  auto info = parse_asm("movq %fs:$1, $0", "=&r,*m,i");
  ASSERT_EQ(info.operands.size(), 3U);
  EXPECT_TRUE(info.operands[0].early_clobber);
  EXPECT_EQ(info.operands[1].cls, AsmOperandClass::Mem);
  EXPECT_TRUE(info.operands[1].indirect);
  EXPECT_EQ(info.operands[2].cls, AsmOperandClass::Imm);
  EXPECT_EQ(info.operands[2].arg, 1);
}

TEST(InlineAsm, InternDedups) {
  foptim::fir::Context ctx{nullptr};
  const auto a = ctx->intern_asm(parse_asm("pause", "", true));
  const auto b = ctx->intern_asm(parse_asm("pause", "", true));
  const auto c = ctx->intern_asm(parse_asm("pause", "", false));
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
  EXPECT_EQ(ctx->asm_info(a).tmpl, "pause");
}

namespace {
std::vector<foptim::u8> assemble_ok(std::string_view text) {
  std::vector<foptim::u8> bytes;
  std::string err;
  EXPECT_TRUE(foptim::codegen::assemble_att(text, bytes, err)) << err;
  return bytes;
}
using Bytes = std::vector<foptim::u8>;
}  // namespace

TEST(InlineAsmEncode, SimpleInstructions) {
  EXPECT_EQ(assemble_ok("pause"), (Bytes{0xf3, 0x90}));
  EXPECT_EQ(assemble_ok("rdtsc"), (Bytes{0x0f, 0x31}));
  EXPECT_EQ(assemble_ok("mfence"), (Bytes{0x0f, 0xae, 0xf0}));
  EXPECT_EQ(assemble_ok("cpuid"), (Bytes{0x0f, 0xa2}));
  EXPECT_EQ(assemble_ok(""), Bytes{});
}

TEST(InlineAsmEncode, SegmentAndLock) {
  EXPECT_EQ(assemble_ok("movq %fs:0, %rax"),
            (Bytes{0x64, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0}));
  EXPECT_EQ(assemble_ok("lock; xaddl %eax, (%rdi)"),
            (Bytes{0xf0, 0x0f, 0xc1, 0x07}));
}

TEST(InlineAsmEncode, LocalLabels) {
  // the jump over the nop is resolved inside the chunk
  EXPECT_EQ(assemble_ok("jmp 1f\nnop\n1:"), (Bytes{0xeb, 0x01, 0x90}));
}

TEST(InlineAsmEncode, ErrorsAreReported) {
  Bytes bytes;
  std::string err;
  EXPECT_FALSE(
      foptim::codegen::assemble_att("notaninstruction %rax", bytes, err));
  EXPECT_NE(err.find("notaninstruction"), std::string::npos);
  err.clear();
  EXPECT_FALSE(foptim::codegen::assemble_att("call foo", bytes, err));
}

TEST(InlineAsmEncode, TemplateExpansion) {
  using foptim::codegen::AsmOperandValue;
  using foptim::fmir::CReg;
  using foptim::fmir::MArgument;
  using foptim::fmir::Type;
  using foptim::fmir::VReg;
  foptim::fmir::MAsmBlock block;
  block.tmpl = foptim::IRString{"addq $2, $0; movl ${1:k}, $$5; # $="};
  block.operands.resize(3);
  block.operands[2].cls = foptim::fir::AsmOperandClass::Imm;
  std::vector<AsmOperandValue> ops(3);
  ops[0] = {true, MArgument{VReg{CReg::A, Type::Int64}, Type::Int64}};
  ops[1] = {true, MArgument{VReg{CReg::R9, Type::Int64}, Type::Int64}};
  ops[2] = {true, MArgument::Int(7, Type::Int64)};
  EXPECT_EQ(foptim::codegen::expand_asm_template(block, ops, 4),
            "addq $7, %rax; movl %r9d, $5; # 4");
}
