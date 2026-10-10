; RUN: %foffcc %s --print-fir --passes "IntrinConv,IntrinExpand" --no-reorder-funcs | FileCheck %s
; IntrinConv turns the fill loops into Memset (value of any type, count in
; elements), IntrinExpand has to advance by the size of the value.

define void @fill_i8(ptr %p) {
; CHECK-LABEL: func fill_i8
; CHECK: Store({{.*}}, 7:i8)
; CHECK: PtrAdd({{.*}}, 1:i64)
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %n, %loop ]
  %a = getelementptr inbounds i8, ptr %p, i64 %i
  store i8 7, ptr %a
  %n = add i64 %i, 1
  %c = icmp ult i64 %n, 100
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

define void @fill_i16(ptr %p) {
; CHECK-LABEL: func fill_i16
; CHECK: Store({{.*}}, 300:i16)
; CHECK: PtrAdd({{.*}}, 2:i64)
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %n, %loop ]
  %a = getelementptr inbounds i16, ptr %p, i64 %i
  store i16 300, ptr %a
  %n = add i64 %i, 1
  %c = icmp ult i64 %n, 100
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

define void @fill_i32(ptr %p) {
; CHECK-LABEL: func fill_i32
; CHECK: Store({{.*}}, 1:i32)
; CHECK: PtrAdd({{.*}}, 4:i64)
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %n, %loop ]
  %a = getelementptr inbounds i32, ptr %p, i64 %i
  store i32 1, ptr %a
  %n = add i64 %i, 1
  %c = icmp ult i64 %n, 100
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

define void @fill_i32_big(ptr %p) {
; CHECK-LABEL: func fill_i32_big
; CHECK: Store({{.*}}, 32:i32)
; CHECK: PtrAdd({{.*}}, 4:i64)
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %n, %loop ]
  %a = getelementptr inbounds i32, ptr %p, i64 %i
  store i32 32, ptr %a
  %n = add i64 %i, 1
  %c = icmp ult i64 %n, 100
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

define void @fill_i64(ptr %p) {
; CHECK-LABEL: func fill_i64
; CHECK: Store({{.*}}, 5:i64)
; CHECK: PtrAdd({{.*}}, 8:i64)
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %n, %loop ]
  %a = getelementptr inbounds i64, ptr %p, i64 %i
  store i64 5, ptr %a
  %n = add i64 %i, 1
  %c = icmp ult i64 %n, 100
  br i1 %c, label %loop, label %exit
exit:
  ret void
}
