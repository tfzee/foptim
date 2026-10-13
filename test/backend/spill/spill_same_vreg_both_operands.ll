; Regression test: a spilled vreg that is both operands of one instruction
; (x + x) used to be rewritten only in one operand, the other use was spilled
; again in a later round into a second slot that nothing ever wrote (and the
; frame grew by one slot per such use). Every value has exactly one slot.
; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s

; CHECK: func f
; CHECK-NEXT:  0:
; CHECK-NEXT:    $rsp: i64 -= 144: i64

declare void @use(double)

define double @f(ptr %p) {
  %g0 = getelementptr inbounds double, ptr %p, i64 0
  %x0 = load double, ptr %g0
  %g1 = getelementptr inbounds double, ptr %p, i64 1
  %x1 = load double, ptr %g1
  %g2 = getelementptr inbounds double, ptr %p, i64 2
  %x2 = load double, ptr %g2
  %g3 = getelementptr inbounds double, ptr %p, i64 3
  %x3 = load double, ptr %g3
  %g4 = getelementptr inbounds double, ptr %p, i64 4
  %x4 = load double, ptr %g4
  %g5 = getelementptr inbounds double, ptr %p, i64 5
  %x5 = load double, ptr %g5
  %g6 = getelementptr inbounds double, ptr %p, i64 6
  %x6 = load double, ptr %g6
  %g7 = getelementptr inbounds double, ptr %p, i64 7
  %x7 = load double, ptr %g7
  %g8 = getelementptr inbounds double, ptr %p, i64 8
  %x8 = load double, ptr %g8
  %g9 = getelementptr inbounds double, ptr %p, i64 9
  %x9 = load double, ptr %g9
  %g10 = getelementptr inbounds double, ptr %p, i64 10
  %x10 = load double, ptr %g10
  %g11 = getelementptr inbounds double, ptr %p, i64 11
  %x11 = load double, ptr %g11
  %g12 = getelementptr inbounds double, ptr %p, i64 12
  %x12 = load double, ptr %g12
  %g13 = getelementptr inbounds double, ptr %p, i64 13
  %x13 = load double, ptr %g13
  %g14 = getelementptr inbounds double, ptr %p, i64 14
  %x14 = load double, ptr %g14
  %g15 = getelementptr inbounds double, ptr %p, i64 15
  %x15 = load double, ptr %g15
  %g16 = getelementptr inbounds double, ptr %p, i64 16
  %x16 = load double, ptr %g16
  %g17 = getelementptr inbounds double, ptr %p, i64 17
  %x17 = load double, ptr %g17
  %g18 = getelementptr inbounds double, ptr %p, i64 18
  %x18 = load double, ptr %g18
  %g19 = getelementptr inbounds double, ptr %p, i64 19
  %x19 = load double, ptr %g19
  %g20 = getelementptr inbounds double, ptr %p, i64 20
  %x20 = load double, ptr %g20
  %g21 = getelementptr inbounds double, ptr %p, i64 21
  %x21 = load double, ptr %g21
  call void @use(double 0.0)
  %d0 = fadd double %x0, %x0
  %d1 = fadd double %x1, %x1
  %d2 = fadd double %x2, %x2
  %d3 = fadd double %x3, %x3
  %d4 = fadd double %x4, %x4
  %d5 = fadd double %x5, %x5
  %d6 = fadd double %x6, %x6
  %d7 = fadd double %x7, %x7
  %d8 = fadd double %x8, %x8
  %d9 = fadd double %x9, %x9
  %d10 = fadd double %x10, %x10
  %d11 = fadd double %x11, %x11
  %d12 = fadd double %x12, %x12
  %d13 = fadd double %x13, %x13
  %d14 = fadd double %x14, %x14
  %d15 = fadd double %x15, %x15
  %d16 = fadd double %x16, %x16
  %d17 = fadd double %x17, %x17
  %d18 = fadd double %x18, %x18
  %d19 = fadd double %x19, %x19
  %d20 = fadd double %x20, %x20
  %d21 = fadd double %x21, %x21
  %s1 = fadd double %d0, %d1
  %s2 = fadd double %s1, %d2
  %s3 = fadd double %s2, %d3
  %s4 = fadd double %s3, %d4
  %s5 = fadd double %s4, %d5
  %s6 = fadd double %s5, %d6
  %s7 = fadd double %s6, %d7
  %s8 = fadd double %s7, %d8
  %s9 = fadd double %s8, %d9
  %s10 = fadd double %s9, %d10
  %s11 = fadd double %s10, %d11
  %s12 = fadd double %s11, %d12
  %s13 = fadd double %s12, %d13
  %s14 = fadd double %s13, %d14
  %s15 = fadd double %s14, %d15
  %s16 = fadd double %s15, %d16
  %s17 = fadd double %s16, %d17
  %s18 = fadd double %s17, %d18
  %s19 = fadd double %s18, %d19
  %s20 = fadd double %s19, %d20
  %s21 = fadd double %s20, %d21
  ret double %s21
}
