; Hand written CHECKs: the output is the PrintAnalysis debug dump, which update_fir_tests.py does not know.
; RUN: %foffcc %s --passes 'PrintAnalysis{show=["memssa"]}' --print-fir 2>&1 | FileCheck %s
; The stores of the two arms join in a phi. The load of %a sees the store of
; one arm, so the phi is its clobber, the load of %b sees the other arm.
define i32 @diamond(ptr noalias %a, ptr noalias %b, i1 %c) {
; CHECK-LABEL: memory ssa of diamond
; CHECK: ; 0 = LiveOnEntry
; CHECK: bb0:
; CHECK-NEXT: ; MemoryUse(0) clobber=0
; CHECK: bb1:
; CHECK-NEXT: ; [[S1:[0-9]+]] = MemoryDef(0)
; CHECK: bb2:
; CHECK-NEXT: ; [[S2:[0-9]+]] = MemoryDef(0)
; CHECK: bb3:
; CHECK-NEXT: ; [[PHI:[0-9]+]] = MemoryPhi([[S1]] [bb1], [[S2]] [bb2])
; CHECK-NEXT: ; MemoryUse([[PHI]]) clobber=[[PHI]]
; CHECK-NEXT: ; MemoryUse([[PHI]]) clobber=[[PHI]]
entry:
  %x = load i32, ptr %a
  br i1 %c, label %l, label %r
l:
  store i32 1, ptr %b
  br label %j
r:
  store i32 2, ptr %a
  br label %j
j:
  %y = load i32, ptr %a
  %z = load i32, ptr %b
  %s = add i32 %y, %z
  ret i32 %s
}
