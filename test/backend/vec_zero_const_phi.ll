; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; RUN: %foffcc %s %t.o

; an all zero vector constant passed as block argument
define <8 x i32> @f(ptr %a, i64 %n) {
; CHECK-LABEL: func f
; CHECK: {{(clear|vXor).*}}i32x8
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i1, %loop ]
  %acc = phi <8 x i32> [ zeroinitializer, %entry ], [ %acc1, %loop ]
  %p = getelementptr <8 x i32>, ptr %a, i64 %i
  %x = load <8 x i32>, ptr %p
  %acc1 = add <8 x i32> %acc, %x
  %i1 = add nuw nsw i64 %i, 1
  %cnd = icmp slt i64 %i1, %n
  br i1 %cnd, label %loop, label %exit
exit:
  ret <8 x i32> %acc1
}
