; RUN: %foffcc %s %t.o
; Add and subtract of partial integer vectors (8 and 4 bytes in an xmm) have to
; encode. A hand written RUN line on purpose, the test only checks the encoder.
define <8 x i8> @add_v8i8(<8 x i8> %a, <8 x i8> %b) {
  %r = add <8 x i8> %a, %b
  ret <8 x i8> %r
}
define <4 x i8> @sub_v4i8(<4 x i8> %a, <4 x i8> %b) {
  %r = sub <4 x i8> %a, %b
  ret <4 x i8> %r
}
define <4 x i16> @add_v4i16(<4 x i16> %a, <4 x i16> %b) {
  %r = add <4 x i16> %a, %b
  ret <4 x i16> %r
}
define <2 x i32> @sub_v2i32(<2 x i32> %a, <2 x i32> %b) {
  %r = sub <2 x i32> %a, %b
  ret <2 x i32> %r
}
