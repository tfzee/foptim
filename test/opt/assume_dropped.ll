; RUN: %foffcc %s --print-fir --passes "DCE" | FileCheck %s
; llvm.assume is dropped by the loader.

declare void @llvm.assume(i1)

; CHECK-LABEL: func f<
; CHECK-NOT: assume
; CHECK: Return
define i64 @f(i64 %a) {
  %c = icmp ne i64 %a, 0
  call void @llvm.assume(i1 %c)
  ret i64 %a
}
