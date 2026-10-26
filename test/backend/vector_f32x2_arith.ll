; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; a <2 x float> in memory is 8 bytes, the packed arithmetic reads 16, so the
; operand has to be loaded into a register first

define <2 x float> @add_mem(ptr %p, <2 x float> %a) {
  %b = load <2 x float>, ptr %p, align 8
  %c = fadd <2 x float> %a, %b
  ret <2 x float> %c
}

define <2 x float> @mul_mem(ptr %p, <2 x float> %a) {
  %b = load <2 x float>, ptr %p, align 8
  %c = fmul <2 x float> %b, %a
  ret <2 x float> %c
}

define <2 x float> @sub_mem(ptr %p, <2 x float> %a) {
  %b = load <2 x float>, ptr %p, align 8
  %c = fsub <2 x float> %a, %b
  ret <2 x float> %c
}

define void @add_loop(ptr noalias %d, ptr noalias %s, i64 %n) {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i2, %loop ]
  %ps = getelementptr <2 x float>, ptr %s, i64 %i
  %pd = getelementptr <2 x float>, ptr %d, i64 %i
  %a = load <2 x float>, ptr %ps, align 8
  %b = load <2 x float>, ptr %pd, align 8
  %c = fadd <2 x float> %a, %b
  store <2 x float> %c, ptr %pd, align 8
  %i2 = add i64 %i, 1
  %e = icmp eq i64 %i2, %n
  br i1 %e, label %out, label %loop
out:
  ret void
}
