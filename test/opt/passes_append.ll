; RUN: %foffcc %s --print-fir --passes "Mem2Reg" | FileCheck %s --check-prefix=ONLY
; RUN: %foffcc %s --print-fir --passes "+DCE" | FileCheck %s --check-prefix=APP
; RUN: not %foffcc %s --print-fir --passes "NoSuchPass" 2>&1 | FileCheck %s --check-prefix=ERR

; ONLY: IntAdd
; APP: IntAdd
; ERR: Unknown FIR pass 'NoSuchPass'
define i32 @f(i32 %x) {
  %r = add i32 %x, 1
  ret i32 %r
}
