; RUN: %foffcc %s --print-fir --passes "InstSimplify" | FileCheck %s
; zext(trunc x to i1) must mask with 1, not with the byte size of i1

; CHECK-LABEL: func f
; CHECK-NOT: 255
; CHECK: And({{.*}}, 1:i8)
define i32 @f(i8 %x) {
  %t = trunc i8 %x to i1
  %z = zext i1 %t to i32
  ret i32 %z
}
