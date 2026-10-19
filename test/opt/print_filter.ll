; RUN: %foffcc %s --print-fir --print-filter 'keep_a,*_b' --passes "DCE" | FileCheck %s
; RUN: %foffcc %s --print-mir --print-filter 'keep*' --no-reorder-funcs | FileCheck %s --check-prefix=MIR
; Handwritten, the CHECKs pin which functions are printed at all.
; CHECK: func keep_a
; CHECK-NOT: func drop
; CHECK: func keep_b
; CHECK-NOT: func drop
; MIR: func keep_a
; MIR-NOT: func drop
define i32 @keep_a(i32 %x) {
  ret i32 %x
}
define i32 @drop(i32 %x) {
  ret i32 %x
}
define i32 @keep_b(i32 %x) {
  ret i32 %x
}
