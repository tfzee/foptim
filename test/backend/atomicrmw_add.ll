; Regression test: atomicrmw add must select `lock xadd [ptr], reg` with the
; pointer as a memory operand (not a plain register) and encode successfully.
; RUN: %foffcc %s --print-mir | FileCheck %s
; RUN: %foffcc %s %t.o

define i32 @atomic_add(ptr %p, i32 %v) {
; CHECK: func atomic_add ($2: i64, $3: i32, )
; CHECK: LockXAdd2([$rdi]: i32, $eax: i32, )
  %r = atomicrmw add ptr %p, i32 %v seq_cst
  ret i32 %r
}
