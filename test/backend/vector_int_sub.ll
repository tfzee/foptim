; RUN: %foffcc %s %t.o
; vsub of integer vectors must encode (three operand VEX form, not legacy psubd/psubq)

define <2 x i64> @sub64(<2 x i64> %a, <2 x i64> %b) {
  %r = sub <2 x i64> %a, %b
  ret <2 x i64> %r
}

define <4 x i32> @sub32(<4 x i32> %a, <4 x i32> %b) {
  %r = sub <4 x i32> %a, %b
  ret <4 x i32> %r
}
