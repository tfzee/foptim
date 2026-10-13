; The i64 init store and i64 load are split into the i32 pieces so the
; accumulator is promoted to registers.
; RUN: %foffcc %s --print-fir --passes "InstSimplify,SORA,Mem2Reg,InstSimplify,DCE" | FileCheck %s

define i64 @f(ptr %in, i32 %n) {
; CHECK-LABEL: func f
; CHECK-NOT: Load
; CHECK-NOT: Store
  %acc = alloca [8 x i8]
  store i64 0, ptr %acc
  %hi = getelementptr i8, ptr %acc, i64 4
  br label %loop
loop:
  %i = phi i32 [0, %0], [%i2, %loop]
  %lo0 = load i32, ptr %acc
  %hi0 = load i32, ptr %hi
  %lo1 = add i32 %lo0, %i
  %hi1 = add i32 %hi0, 1
  store i32 %lo1, ptr %acc
  store i32 %hi1, ptr %hi
  %i2 = add i32 %i, 1
  %c = icmp slt i32 %i2, %n
  br i1 %c, label %loop, label %exit
exit:
  %r = load i64, ptr %acc
  ret i64 %r
}
