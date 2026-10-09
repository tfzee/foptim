; RUN: %foffcc %s --print-fir --passes "SCCP" | FileCheck %s
; Constant folding of unordered fcmp predicates used to hit TODO("IMPL").
define i1 @f() {
  %a = fcmp ult double 1.0, 2.0
  %b = fcmp uno double 0x7FF8000000000000, 2.0
  %c = fcmp ord double 0x7FF8000000000000, 2.0
  %d = and i1 %a, %b
  %e = and i1 %d, %c
  ret i1 %e
}
; CHECK: And(1:i1, 1:i1)
; CHECK: And(1:i1, 0:i1)
; CHECK: Return(0:i1)
