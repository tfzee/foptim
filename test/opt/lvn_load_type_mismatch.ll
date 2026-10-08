; RUN: %foffcc %s --print-fir --passes "LVN" | FileCheck %s
; Two loads of the same address with the same size but different types
; (ptr and <2 x float>) must not be merged.

; CHECK-LABEL: func f
; CHECK: ptr = Load
; CHECK: 2@f32 = Load
define void @f(ptr %p, ptr %q, ptr %r) {
  %a = load ptr, ptr %p
  store ptr %a, ptr %q
  %b = load <2 x float>, ptr %p
  store <2 x float> %b, ptr %r
  ret void
}
