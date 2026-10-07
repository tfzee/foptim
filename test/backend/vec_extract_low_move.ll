; Regression test: V.ExtractLow of a <4 x i64> that is also stored becomes a
; narrowing ymm -> xmm register move, which the encoder used to emit as
; `vmovupd xmm, ymm` (not encodable, Zyan op failed).
; RUN: %foffcc %s --print-mir | FileCheck %s
; RUN: %foffcc %s %t.o
; RUN: llvm-objdump -d --no-show-raw-insn -M intel %t.o | FileCheck %s --check-prefix=ASM

define void @f(ptr %src, ptr %d1, ptr %d2) {
; CHECK: func f
; CHECK: $xmm0: i64x2 = $ymm0: i64x4
; ASM: vmovupd xmm0, xmm0
  %v = load <4 x i64>, ptr %src, align 8
  store <4 x i64> %v, ptr %d1, align 8
  %a = load i64, ptr %src, align 8
  %p = getelementptr i8, ptr %src, i64 8
  %b = load i64, ptr %p, align 8
  store i64 %a, ptr %d2, align 8
  %q = getelementptr i8, ptr %d2, i64 8
  store i64 %b, ptr %q, align 8
  ret void
}
