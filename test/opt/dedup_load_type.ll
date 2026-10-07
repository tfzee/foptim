; RUN: %foffcc %s --print-fir --passes "FunctionDedupSame,FunctionDedupDiff" | FileCheck %s
; Regression test: functions that only differ in the type of a load/store must
; not be merged (a `load i8` was replaced by the `load ptr` of its twin and
; copied 8 bytes instead of 1).
; CHECK-NOT: MERGED
; CHECK: func copy_i8<CC: C, LINK: linkonceODR, > Uses: 1
; CHECK: i8 = Load
; CHECK: func copy_ptr<CC: C, LINK: linkonceODR, > Uses: 1
; CHECK: ptr = Load
; CHECK: func fill8<CC: C, LINK: linkonceODR, > Uses: 1
; CHECK: i8 = Store
; CHECK: func fill16<CC: C, LINK: linkonceODR, > Uses: 1
; CHECK: i16 = Store

define linkonce_odr void @copy_i8(ptr %dst, ptr %src) {
  %v = load i8, ptr %src, align 1
  store i8 %v, ptr %dst, align 1
  ret void
}

define linkonce_odr void @copy_ptr(ptr %dst, ptr %src) {
  %v = load ptr, ptr %src, align 8
  store ptr %v, ptr %dst, align 8
  ret void
}

define void @user(ptr %a, ptr %b) {
  call void @copy_i8(ptr %a, ptr %b)
  call void @copy_ptr(ptr %a, ptr %b)
  ret void
}

; stores of different widths
define linkonce_odr void @fill8(ptr %a) {
  store i8 5, ptr %a, align 1
  ret void
}

define linkonce_odr void @fill16(ptr %a) {
  store i16 5, ptr %a, align 2
  ret void
}

define void @user2(ptr %a) {
  call void @fill8(ptr %a)
  call void @fill16(ptr %a)
  ret void
}
