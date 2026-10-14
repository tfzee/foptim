; RUN: %foffcc %s --print-fir --passes "LLVMIntrinsicLowering,LICM" | FileCheck %s
; A memcmp of loop invariant pointers leaves the loop when the loop writes no
; memory and the header runs on every entry.
declare i32 @memcmp(ptr, ptr, i64)

define i32 @hoist(ptr %a, ptr %b, i64 %n) {
; CHECK-LABEL: func hoist
; CHECK: bb0
; CHECK: INTRIN:MemCmp
; CHECK: bb1
; CHECK-NOT: INTRIN:MemCmp
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc1, %loop ]
  %c = call i32 @memcmp(ptr %a, ptr %b, i64 24)
  %acc1 = add i32 %acc, %c
  %i1 = add i64 %i, 1
  %cond = icmp ult i64 %i1, %n
  br i1 %cond, label %loop, label %exit
exit:
  ret i32 %acc1
}

; a store in the loop may change the bytes
define i32 @stays(ptr %a, ptr %b, i64 %n) {
; CHECK-LABEL: func stays
; CHECK: bb1
; CHECK: INTRIN:MemCmp
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc1, %loop ]
  %c = call i32 @memcmp(ptr %a, ptr %b, i64 24)
  store i32 %c, ptr %a
  %acc1 = add i32 %acc, %c
  %i1 = add i64 %i, 1
  %cond = icmp ult i64 %i1, %n
  br i1 %cond, label %loop, label %exit
exit:
  ret i32 %acc1
}

; a non invariant argument stays
define i32 @variant(ptr %a, ptr %b, i64 %n) {
; CHECK-LABEL: func variant
; CHECK: bb1
; CHECK: INTRIN:MemCmp
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %acc = phi i32 [ 0, %entry ], [ %acc1, %loop ]
  %p = getelementptr i8, ptr %a, i64 %i
  %c = call i32 @memcmp(ptr %p, ptr %b, i64 24)
  %acc1 = add i32 %acc, %c
  %i1 = add i64 %i, 1
  %cond = icmp ult i64 %i1, %n
  br i1 %cond, label %loop, label %exit
exit:
  ret i32 %acc1
}
