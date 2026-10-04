; RUN: %foffcc %s --print-fir | FileCheck %s
; A big callee that is handed a pointer into a stack object gets inlined so the
; object can be promoted to registers afterwards.
; CHECK-LABEL: func caller<
; CHECK-NOT: Alloca
; CHECK-NOT: Call(
; CHECK: Return
%obj = type { i64, i64, i64 }
define linkonce_odr void @fill(ptr %o, i64 %n) {
  %p1 = getelementptr inbounds %obj, ptr %o, i32 0, i32 1
  store i64 %n, ptr %p1
  %z = icmp eq i64 %n, 0
  br i1 %z, label %zero, label %nonzero
zero:
  %pz = getelementptr inbounds %obj, ptr %o, i32 0, i32 0
  store i64 7, ptr %pz
  %pz2 = getelementptr inbounds %obj, ptr %o, i32 0, i32 2
  store i64 8, ptr %pz2
  ret void
nonzero:
  %v = load i64, ptr %p1
  %t0 = add i64 %v, 1
  %u0 = xor i64 %t0, %n
  %t1 = add i64 %v, 2
  %u1 = xor i64 %t1, %n
  %t2 = add i64 %v, 3
  %u2 = xor i64 %t2, %n
  %t3 = add i64 %v, 4
  %u3 = xor i64 %t3, %n
  %t4 = add i64 %v, 5
  %u4 = xor i64 %t4, %n
  %t5 = add i64 %v, 6
  %u5 = xor i64 %t5, %n
  %t6 = add i64 %v, 7
  %u6 = xor i64 %t6, %n
  %t7 = add i64 %v, 8
  %u7 = xor i64 %t7, %n
  %t8 = add i64 %v, 9
  %u8 = xor i64 %t8, %n
  %t9 = add i64 %v, 10
  %u9 = xor i64 %t9, %n
  %t10 = add i64 %v, 11
  %u10 = xor i64 %t10, %n
  %t11 = add i64 %v, 12
  %u11 = xor i64 %t11, %n
  %t12 = add i64 %v, 13
  %u12 = xor i64 %t12, %n
  %t13 = add i64 %v, 14
  %u13 = xor i64 %t13, %n
  %t14 = add i64 %v, 15
  %u14 = xor i64 %t14, %n
  %t15 = add i64 %v, 16
  %u15 = xor i64 %t15, %n
  %t16 = add i64 %v, 17
  %u16 = xor i64 %t16, %n
  %t17 = add i64 %v, 18
  %u17 = xor i64 %t17, %n
  %t18 = add i64 %v, 19
  %u18 = xor i64 %t18, %n
  %t19 = add i64 %v, 20
  %u19 = xor i64 %t19, %n
  %t20 = add i64 %v, 21
  %u20 = xor i64 %t20, %n
  %t21 = add i64 %v, 22
  %u21 = xor i64 %t21, %n
  %t22 = add i64 %v, 23
  %u22 = xor i64 %t22, %n
  %t23 = add i64 %v, 24
  %u23 = xor i64 %t23, %n
  %t24 = add i64 %v, 25
  %u24 = xor i64 %t24, %n
  %t25 = add i64 %v, 26
  %u25 = xor i64 %t25, %n
  %t26 = add i64 %v, 27
  %u26 = xor i64 %t26, %n
  %t27 = add i64 %v, 28
  %u27 = xor i64 %t27, %n
  %t28 = add i64 %v, 29
  %u28 = xor i64 %t28, %n
  %t29 = add i64 %v, 30
  %u29 = xor i64 %t29, %n
  %t30 = add i64 %v, 31
  %u30 = xor i64 %t30, %n
  %t31 = add i64 %v, 32
  %u31 = xor i64 %t31, %n
  %t32 = add i64 %v, 33
  %u32 = xor i64 %t32, %n
  %t33 = add i64 %v, 34
  %u33 = xor i64 %t33, %n
  %t34 = add i64 %v, 35
  %u34 = xor i64 %t34, %n
  %t35 = add i64 %v, 36
  %u35 = xor i64 %t35, %n
  %t36 = add i64 %v, 37
  %u36 = xor i64 %t36, %n
  %t37 = add i64 %v, 38
  %u37 = xor i64 %t37, %n
  %t38 = add i64 %v, 39
  %u38 = xor i64 %t38, %n
  %t39 = add i64 %v, 40
  %u39 = xor i64 %t39, %n
  %t40 = add i64 %v, 41
  %u40 = xor i64 %t40, %n
  %t41 = add i64 %v, 42
  %u41 = xor i64 %t41, %n
  %t42 = add i64 %v, 43
  %u42 = xor i64 %t42, %n
  %t43 = add i64 %v, 44
  %u43 = xor i64 %t43, %n
  %t44 = add i64 %v, 45
  %u44 = xor i64 %t44, %n
  %t45 = add i64 %v, 46
  %u45 = xor i64 %t45, %n
  %t46 = add i64 %v, 47
  %u46 = xor i64 %t46, %n
  %t47 = add i64 %v, 48
  %u47 = xor i64 %t47, %n
  %t48 = add i64 %v, 49
  %u48 = xor i64 %t48, %n
  %t49 = add i64 %v, 50
  %u49 = xor i64 %t49, %n
  %t50 = add i64 %v, 51
  %u50 = xor i64 %t50, %n
  %t51 = add i64 %v, 52
  %u51 = xor i64 %t51, %n
  %t52 = add i64 %v, 53
  %u52 = xor i64 %t52, %n
  %t53 = add i64 %v, 54
  %u53 = xor i64 %t53, %n
  %t54 = add i64 %v, 55
  %u54 = xor i64 %t54, %n
  %t55 = add i64 %v, 56
  %u55 = xor i64 %t55, %n
  %t56 = add i64 %v, 57
  %u56 = xor i64 %t56, %n
  %t57 = add i64 %v, 58
  %u57 = xor i64 %t57, %n
  %t58 = add i64 %v, 59
  %u58 = xor i64 %t58, %n
  %t59 = add i64 %v, 60
  %u59 = xor i64 %t59, %n
  %p2 = getelementptr inbounds %obj, ptr %o, i32 0, i32 2
  store i64 %u59, ptr %p2
  %p0 = getelementptr inbounds %obj, ptr %o, i32 0, i32 0
  store i64 %v, ptr %p0
  ret void
}
define i64 @caller(i64 %n) {
  %o = alloca %obj
  call void @fill(ptr %o, i64 %n)
  %p2 = getelementptr inbounds %obj, ptr %o, i32 0, i32 2
  %a = load i64, ptr %p2
  %p0 = getelementptr inbounds %obj, ptr %o, i32 0, i32 0
  %b = load i64, ptr %p0
  %r = add i64 %a, %b
  ret i64 %r
}
define i64 @caller2(i64 %n) {
  %o = alloca %obj
  call void @fill(ptr %o, i64 %n)
  %p0 = getelementptr inbounds %obj, ptr %o, i32 0, i32 0
  %b = load i64, ptr %p0
  ret i64 %b
}
