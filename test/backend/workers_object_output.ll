; Hand written, there is no MIR to check, only that the object is written.
; RUN: %foffcc %s %t.o --workers 4
; RUN: test -s %t.o

define i64 @a(i64 %x) {
  %r = add i64 %x, 1
  ret i64 %r
}

define i64 @b(i64 %x, i64 %y) {
  %c = icmp slt i64 %x, %y
  br i1 %c, label %t, label %f
t:
  %m = mul i64 %x, %y
  ret i64 %m
f:
  %s = sub i64 %x, %y
  ret i64 %s
}

define i64 @c(i64 %x) {
  %p = call i64 @a(i64 %x)
  %q = call i64 @b(i64 %p, i64 %x)
  ret i64 %q
}
