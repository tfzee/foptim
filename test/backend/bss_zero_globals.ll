; Zero initialized globals without relocations go to .bss (NOBITS) instead of
; being written as zeros into .data. Other globals stay in .data.
; RUN: %foffcc %s %t.o && llvm-readelf -S -s -W %t.o | FileCheck %s

; CHECK: .data PROGBITS {{.*}} 000008
; CHECK: .bss NOBITS {{.*}} 400020
; CHECK-DAG: {{[0-9a-f]+}} 4194304 OBJECT GLOBAL DEFAULT {{[0-9]+}} big_zero
; CHECK-DAG: {{[0-9a-f]+}} 8 OBJECT GLOBAL DEFAULT {{[0-9]+}} nonzero
; CHECK-DAG: {{[0-9a-f]+}} 32 OBJECT GLOBAL DEFAULT {{[0-9]+}} small_zero

@big_zero = global [1048576 x i32] zeroinitializer, align 16
@small_zero = global [8 x i32] zeroinitializer, align 16
@nonzero = global [2 x i32] [i32 0, i32 1], align 4

define i32 @f(i64 %i) {
  %p = getelementptr i32, ptr @big_zero, i64 %i
  store i32 7, ptr %p
  %q = getelementptr i32, ptr @small_zero, i64 %i
  %a = load i32, ptr %q
  %r = getelementptr i32, ptr @nonzero, i64 %i
  %b = load i32, ptr %r
  %c = add i32 %a, %b
  ret i32 %c
}
