; RUN: %foffcc %s --print-fir --passes "Mem2Reg,InstSimplify,LVN,SCCP,DCE,FuncPropAnnotator,SLPVectorizer,LegalizeVecs,InstSimplify,DCE" | FileCheck %s
; Callee writes a global: calls must stay scalar.
; Handwritten CHECKs (several functions per module, the update script can not handle that).

define internal float @sq(float %x) {
  %r = fmul float %x, %x
  ret float %r
}
define internal float @impure(float %x) {
  %r = fmul float %x, %x
  store float %r, ptr @g
  ret float %r
}
@g = global float 0.0

define void @f(ptr %a, ptr %b) {
  %a0 = getelementptr float, ptr %a, i64 0
  %a1 = getelementptr float, ptr %a, i64 1
  %a2 = getelementptr float, ptr %a, i64 2
  %a3 = getelementptr float, ptr %a, i64 3
  %b0 = getelementptr float, ptr %b, i64 0
  %b1 = getelementptr float, ptr %b, i64 1
  %b2 = getelementptr float, ptr %b, i64 2
  %b3 = getelementptr float, ptr %b, i64 3
  %x0 = load float, ptr %b0
  %r0 = call float @impure(float %x0)
  store float %r0, ptr %a0
  %x1 = load float, ptr %b1
  %r1 = call float @impure(float %x1)
  store float %r1, ptr %a1
  %x2 = load float, ptr %b2
  %r2 = call float @impure(float %x2)
  store float %r2, ptr %a2
  %x3 = load float, ptr %b3
  %r3 = call float @impure(float %x3)
  store float %r3, ptr %a3
  ret void
}
; CHECK-LABEL: func f<
; CHECK-COUNT-4: Call(impure,
; CHECK-NOT: V.
; CHECK-NOT: 4@f32
