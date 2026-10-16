; RUN: %foffcc %s --print-fir --passes "FuncPropAnnotator,ArgPromotion" | FileCheck %s

; A promoted pointer argument that is also an operand of a pointer select must
; not be loaded again, it already is the loaded value.
define internal double @pick(ptr %a, ptr %b) {
entry:
  %x = load double, ptr %a
  %y = load double, ptr %b
  %c = fcmp olt double %x, %y
  %p = select i1 %c, ptr %b, ptr %a
  br i1 %c, label %l, label %r
l:
  %v = load double, ptr %p
  ret double %v
r:
  %w = load double, ptr %p
  %z = fadd double %w, 1.0
  ret double %z
}

define double @user(double %u, double %v) {
entry:
  %s = alloca double
  %t = alloca double
  store double %u, ptr %s
  store double %v, ptr %t
  %r = call double @pick(ptr %s, ptr %t)
  ret double %r
}

; CHECK-LABEL: func pick<
; CHECK-NEXT:{
; CHECK-NEXT:  bb0(%0{}: f64, %1{}: f64):
; CHECK-NEXT:    %2: i1 = FloatOLT(%0, %1){} NUses:2
; CHECK-NEXT:    %3: f64 = Select(%2, %1, %0){} NUses:2
; CHECK-NOT:     Load
; CHECK:         Return
