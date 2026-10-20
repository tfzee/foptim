; RUN: %foffcc %s %t.o
; XFAIL: *
; Four adjacent bitwise nots are widened by the SLP vectorizer to a vector Not, which the instruction selector has no rule for
; (matcher_patterns.cpp, TODO) and the compiler aborts. Plain vector IR does not hit it, the loader turns xor with -1 into a vector xor.

define void @f(ptr %a, ptr %b) {
  %pb0 = getelementptr i32, ptr %b, i64 0
  %pb1 = getelementptr i32, ptr %b, i64 1
  %pb2 = getelementptr i32, ptr %b, i64 2
  %pb3 = getelementptr i32, ptr %b, i64 3
  %x0 = load i32, ptr %pb0
  %x1 = load i32, ptr %pb1
  %x2 = load i32, ptr %pb2
  %x3 = load i32, ptr %pb3
  %r0 = xor i32 %x0, -1
  %r1 = xor i32 %x1, -1
  %r2 = xor i32 %x2, -1
  %r3 = xor i32 %x3, -1
  %pa0 = getelementptr i32, ptr %a, i64 0
  %pa1 = getelementptr i32, ptr %a, i64 1
  %pa2 = getelementptr i32, ptr %a, i64 2
  %pa3 = getelementptr i32, ptr %a, i64 3
  store i32 %r0, ptr %pa0
  store i32 %r1, ptr %pa1
  store i32 %r2, ptr %pa2
  store i32 %r3, ptr %pa3
  ret void
}
