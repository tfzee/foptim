; RUN: %foffcc %s --print-fir --passes "Inline" | FileCheck %s

; Recursive functions are unrolled into themselves and their callers, every
; function keeps exactly one call (the recursion is never removed). The exact
; depth is not pinned.

; CHECK-LABEL: func rec<
; CHECK: Call(rec,
; CHECK-NOT: Call(
; CHECK-LABEL: func caller<
; CHECK: Call(rec,
; CHECK-NOT: Call(
; CHECK-LABEL: func ping<
; CHECK: Call(ping,
; CHECK-NOT: Call(
; CHECK-LABEL: func pong<
; CHECK: Call(ping,
; CHECK-NOT: Call(
define i32 @rec(i32 %n) {
entry:
  %c = icmp eq i32 %n, 0
  br i1 %c, label %base, label %step
base:
  ret i32 1
step:
  %m = sub i32 %n, 1
  %r = call i32 @rec(i32 %m)
  %s = mul i32 %r, %n
  ret i32 %s
}

define i32 @caller(i32 %x) {
  %r = call i32 @rec(i32 %x)
  ret i32 %r
}

; mutual recursion
define i32 @ping(i32 %n) {
  %c = icmp eq i32 %n, 0
  br i1 %c, label %base, label %step
base:
  ret i32 0
step:
  %m = sub i32 %n, 1
  %r = call i32 @pong(i32 %m)
  %s = add i32 %r, 1
  ret i32 %s
}

define i32 @pong(i32 %n) {
  %c = icmp eq i32 %n, 0
  br i1 %c, label %base, label %step
base:
  ret i32 0
step:
  %m = sub i32 %n, 1
  %r = call i32 @ping(i32 %m)
  %s = add i32 %r, 2
  ret i32 %s
}
