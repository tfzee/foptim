; Hand written CHECKs: the output is the PrintAnalysis debug dump, which update_fir_tests.py does not know.
; RUN: %foffcc %s --passes 'PrintAnalysis{show=["memssa"]}' --print-fir 2>&1 | FileCheck %s
; The store through %q may alias the load of %p in the next iteration, the
; walk goes around the back edge and ends at the phi.
define i32 @loop_alias(ptr %p, ptr %q, i64 %n) {
; CHECK-LABEL: memory ssa of loop_alias
; CHECK: bb1:
; CHECK-NEXT: ; [[PHI:[0-9]+]] = MemoryPhi(0 [bb0], [[ST:[0-9]+]] [bb1])
; CHECK-NEXT: ; MemoryUse([[PHI]]) clobber=[[PHI]]
; CHECK-NEXT: ; [[ST]] = MemoryDef([[PHI]])
entry:
  br label %h
h:
  %i = phi i64 [0, %entry], [%i2, %h]
  %v = load i32, ptr %p
  store i32 %v, ptr %q
  %i2 = add i64 %i, 1
  %cc = icmp ult i64 %i2, %n
  br i1 %cc, label %h, label %e
e:
  ret i32 0
}
