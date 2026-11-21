; Scalar converts and sqrt merge into their destination, so the destination is
; zeroed first to not depend on its old value (false dependency on Intel).
; Hand written CHECKs, the MIR script cannot see the encoding.
; RUN: %foffcc %s %t.o
; RUN: llvm-objdump -d --no-show-raw-insn -M intel %t.o | FileCheck %s --check-prefix=ASM

declare double @llvm.sqrt.f64(double)

define double @f(i64 %a, float %b, double %c) {
; ASM-LABEL: <f>:
; ASM: vxorps [[A:xmm[0-9]+]], [[A]], [[A]]
; ASM-NEXT: vcvtsi2sd [[A]], [[A]], rdi
; ASM: vcvtss2sd {{xmm[0-9]+}}, {{xmm[0-9]+}}, {{xmm[0-9]+}}
; ASM: vsqrtsd {{xmm[0-9]+}}, {{xmm[0-9]+}}, {{xmm[0-9]+}}
  %x = sitofp i64 %a to double
  %y = fpext float %b to double
  %z = call double @llvm.sqrt.f64(double %c)
  %s = fadd double %x, %y
  %s2 = fadd double %s, %z
  ret double %s2
}
