; select of a double on a float compare: the compare mask only fills the low
; dword, vblendvpd reads the top bit of the qword, so it has to be widened.
; RUN: %foffcc %s %t.o
; RUN: llvm-objdump -d --no-show-raw-insn -M intel %t.o | FileCheck %s --check-prefix=ASM

define double @fused(float %c, double %a, double %b) {
; ASM-LABEL: <fused>:
; ASM: vcmpgtss [[M:xmm[0-9]+]], {{xmm[0-9]+}}, {{xmm[0-9]+}}
; ASM-NEXT: vbroadcastss [[M]], [[M]]
; ASM-NEXT: vblendvpd {{xmm[0-9]+}}, {{xmm[0-9]+}}, {{xmm[0-9]+}}, [[M]]
  %t = fcmp ogt float %c, 1.0
  %r = select i1 %t, double %a, double %b
  ret double %r
}

define double @shared(float %c, double %a, double %b, ptr %p) {
; ASM-LABEL: <shared>:
; ASM: vbroadcastss
; ASM: vblendvpd
  %t = fcmp ogt float %c, 1.0
  %r = select i1 %t, double %a, double %b
  %z = zext i1 %t to i32
  store i32 %z, ptr %p
  ret double %r
}
