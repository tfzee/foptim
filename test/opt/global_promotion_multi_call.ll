; RUN: %foffcc %s --print-fir --passes "FuncPropAnnotator,GlobalPromotion" | FileCheck %s
; a global used by one function that is called more than once must stay a global

@seed = internal global i64 1325

; CHECK-LABEL: func next
; CHECK-NOT: Alloca
define i64 @next() {
  %v = load i64, ptr @seed
  %n = add i64 %v, 7
  store i64 %n, ptr @seed
  ret i64 %n
}

define i64 @main() {
  %a = call i64 @next()
  %b = call i64 @next()
  %c = add i64 %a, %b
  ret i64 %c
}
