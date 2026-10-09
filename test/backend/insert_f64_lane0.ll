; Regression test: inserting a double into lane 0 of a <2 x double> used
; vinsertps, which only moves 32 bits and corrupted the value.
; RUN: %foffcc %s %t.o
; RUN: llvm-objdump -d --no-show-raw-insn -M intel %t.o | FileCheck %s --check-prefix=ASM
; ASM-LABEL: <ins0>:
; ASM-NOT: vinsertps
; ASM: vmovsd xmm
define <2 x double> @ins0(<2 x double> %v, double %d) {
  %r = insertelement <2 x double> %v, double %d, i32 0
  ret <2 x double> %r
}
