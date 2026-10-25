; Hand written CHECKs: the output is the PrintAnalysis debug dump, which update_fir_tests.py does not know.
; RUN: %foffcc %s --passes 'PrintAnalysis{show=["memssa"]}' --print-fir 2>&1 | FileCheck %s
; The store in the loop gives the header a phi. The load of %a[i] and of %a
; are not touched by the store to the noalias %b, so they depend on the entry
; state. The load of %b behind the loop sees the store.
define i32 @loop(ptr noalias %a, ptr noalias %b, i64 %n) {
; CHECK-LABEL: memory ssa of loop
; CHECK: bb1:
; CHECK-NEXT: ; [[PHI:[0-9]+]] = MemoryPhi(0 [bb0], [[ST:[0-9]+]] [bb1])
; CHECK-NEXT: ; MemoryUse([[PHI]]) clobber=0
; CHECK-NEXT: ; [[ST]] = MemoryDef([[PHI]])
; CHECK-NEXT: ; MemoryUse([[ST]]) clobber=0
; CHECK: bb2:
; CHECK-NEXT: ; MemoryUse([[ST]]) clobber=[[ST]]
entry:
  br label %h
h:
  %i = phi i64 [0, %entry], [%i2, %h]
  %p = getelementptr i32, ptr %a, i64 %i
  %v = load i32, ptr %p
  store i32 %v, ptr %b
  %w = load i32, ptr %a
  %i2 = add i64 %i, 1
  %cc = icmp ult i64 %i2, %n
  br i1 %cc, label %h, label %e
e:
  %r = load i32, ptr %b
  ret i32 %r
}
