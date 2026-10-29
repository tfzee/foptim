; Initialized globals must be placed at their alignment and the .data section must be aligned to the largest.
; RUN: %foffcc %s %t.o
; RUN: llvm-readelf -SsW %t.o | FileCheck %s
; This test is handwritten, the update script cannot express the readelf stage.

; CHECK: .data {{.*}} WA 0 0 16
; CHECK-DAG: 0000000000000000 {{.*}} a
; CHECK-DAG: 0000000000000010 {{.*}} b
; CHECK-DAG: 0000000000000018 {{.*}} c

@a = global [3 x i8] c"abc", align 1
@b = global i64 5, align 16
@c = global i64 7, align 8

define ptr @f(i32 %x) {
  %t = icmp eq i32 %x, 0
  %p = select i1 %t, ptr @b, ptr @c
  ret ptr %p
}
define ptr @g() {
  ret ptr @a
}
