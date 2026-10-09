; Regression test: extracting lane 1 of a <2 x double> into a double used to
; emit `vpextrq xmm, xmm, 1`, which is not encodable (Zyan op failed).
; RUN: %foffcc %s %t.o
; RUN: llvm-objdump -d --no-show-raw-insn -M intel %t.o | FileCheck %s --check-prefix=ASM

define double @f(ptr %src) {
; ASM: vunpckhpd
  %v = load <2 x double>, ptr %src, align 16
  %e = extractelement <2 x double> %v, i32 1
  ret double %e
}
