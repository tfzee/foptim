; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s

; CHECK-LABEL: func swab16
; CHECK: bswap($ax: i16, $di: i16, )
; CHECK-NEXT: ret
define i16 @swab16(i16 %x) {
  %a = lshr i16 %x, 8
  %b = shl i16 %x, 8
  %c = or i16 %a, %b
  ret i16 %c
}

; CHECK-LABEL: func swab32
; CHECK: bswap($eax: i32, $edi: i32, )
; CHECK-NEXT: ret
define i32 @swab32(i32 %x) {
  %a = lshr i32 %x, 24
  %b = shl i32 %x, 24
  %c = lshr i32 %x, 8
  %c1 = and i32 %c, 65280
  %d = shl i32 %x, 8
  %d1 = and i32 %d, 16711680
  %e = or i32 %a, %b
  %f = or i32 %e, %c1
  %g = or i32 %f, %d1
  ret i32 %g
}

; CHECK-LABEL: func swab64
; CHECK: bswap($rax: i64, $rdi: i64, )
; CHECK-NEXT: ret
define i64 @swab64(i64 %x) {
  %a = lshr i64 %x, 56
  %b = shl i64 %x, 56
  %c = lshr i64 %x, 40
  %c1 = and i64 %c, 65280
  %d = shl i64 %x, 40
  %d1 = and i64 %d, 71776119061217280
  %e = lshr i64 %x, 24
  %e1 = and i64 %e, 16711680
  %f = shl i64 %x, 24
  %f1 = and i64 %f, 280375465082880
  %g = lshr i64 %x, 8
  %g1 = and i64 %g, 4278190080
  %h = shl i64 %x, 8
  %h1 = and i64 %h, 1095216660480
  %o1 = or i64 %a, %b
  %o2 = or i64 %o1, %c1
  %o3 = or i64 %o2, %d1
  %o4 = or i64 %o3, %e1
  %o5 = or i64 %o4, %f1
  %o6 = or i64 %o5, %g1
  %o7 = or i64 %o6, %h1
  ret i64 %o7
}
