; Hand written CHECKs: the output is the PrintAnalysis debug dump, which update_fir_tests.py does not know.
; RUN: %foffcc %s --passes 'PrintAnalysis{show=["memssa"]}' --print-fir 2>&1 | FileCheck %s
; Only one arm writes memory the final load can see, the other arm writes
; through a noalias pointer. A phi would be useless for the load: both paths
; lead to the same def, so the walk skips the phi and returns the store.
define i32 @one_arm(ptr noalias %a, ptr noalias %b, i1 %c) {
; CHECK-LABEL: memory ssa of one_arm
; CHECK: bb0:
; CHECK-NEXT: ; [[S0:[0-9]+]] = MemoryDef(0)
; CHECK: bb1:
; CHECK-NEXT: ; [[S1:[0-9]+]] = MemoryDef([[S0]])
; CHECK: bb3:
; CHECK-NEXT: ; [[PHI:[0-9]+]] = MemoryPhi([[S1]] [bb1], [[S0]] [bb2])
; CHECK-NEXT: ; MemoryUse([[PHI]]) clobber=[[S0]]
entry:
  store i32 5, ptr %a
  br i1 %c, label %l, label %r
l:
  store i32 1, ptr %b
  br label %j
r:
  br label %j
j:
  %y = load i32, ptr %a
  ret i32 %y
}
