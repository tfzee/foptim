; Regression test: LoopSimplify's exit value propagation replaced the induction
; variable of the middle loop with its exit value inside the loop itself (the
; exit block dominates the loop because of the outer loop). Exit code 0 is correct.
; RUN: %foffcc %s %t.o
; RUN: clang++ %t.o -o %t.out
; RUN: %t.out

%struct.IJb = type { i8 }

$_ZN3IJbIiEclEPKiPiS2_iii = comdat any

@_ZL2xx = internal global [150 x i32] zeroinitializer, align 16
@_ZL2zz = internal global [30000 x i32] zeroinitializer, align 16
@_ZL2yy = internal global [200 x i32] zeroinitializer, align 16

; Function Attrs: mustprogress noinline nounwind optnone sspstrong uwtable
define noundef i32 @_Z10blocksize1IiEiv() #0 {
  ret i32 600
}

; Function Attrs: mustprogress noinline norecurse nounwind optnone sspstrong uwtable
define noundef i32 @main() #1 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca %struct.IJb, align 1
  %5 = alloca i64, align 8
  %6 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  store i32 0, ptr %2, align 4
  br label %7

7:                                                ; preds = %17, %0
  %8 = load i32, ptr %2, align 4
  %9 = icmp slt i32 %8, 150
  br i1 %9, label %10, label %20

10:                                               ; preds = %7
  %11 = load i32, ptr %2, align 4
  %12 = srem i32 %11, 7
  %13 = add i32 %12, 1
  %14 = load i32, ptr %2, align 4
  %15 = sext i32 %14 to i64
  %16 = getelementptr inbounds [150 x i32], ptr @_ZL2xx, i64 0, i64 %15
  store i32 %13, ptr %16, align 4
  br label %17

17:                                               ; preds = %10
  %18 = load i32, ptr %2, align 4
  %19 = add i32 %18, 1
  store i32 %19, ptr %2, align 4
  br label %7, !llvm.loop !6

20:                                               ; preds = %7
  store i32 0, ptr %3, align 4
  br label %21

21:                                               ; preds = %30, %20
  %22 = load i32, ptr %3, align 4
  %23 = icmp slt i32 %22, 30000
  br i1 %23, label %24, label %33

24:                                               ; preds = %21
  %25 = load i32, ptr %3, align 4
  %26 = srem i32 %25, 11
  %27 = load i32, ptr %3, align 4
  %28 = sext i32 %27 to i64
  %29 = getelementptr inbounds [30000 x i32], ptr @_ZL2zz, i64 0, i64 %28
  store i32 %26, ptr %29, align 4
  br label %30

30:                                               ; preds = %24
  %31 = load i32, ptr %3, align 4
  %32 = add i32 %31, 1
  store i32 %32, ptr %3, align 4
  br label %21, !llvm.loop !8

33:                                               ; preds = %21
  %34 = call noundef i32 @_Z10blocksize1IiEiv()
  call void @_ZN3IJbIiEclEPKiPiS2_iii(ptr noundef nonnull align 1 dereferenceable(1) %4, ptr noundef @_ZL2xx, ptr noundef @_ZL2yy, ptr noundef @_ZL2zz, i32 noundef 200, i32 noundef 150, i32 noundef %34)
  store i64 0, ptr %5, align 8
  store i32 0, ptr %6, align 4
  br label %35

35:                                               ; preds = %49, %33
  %36 = load i32, ptr %6, align 4
  %37 = icmp slt i32 %36, 200
  br i1 %37, label %38, label %52

38:                                               ; preds = %35
  %39 = load i32, ptr %6, align 4
  %40 = sext i32 %39 to i64
  %41 = getelementptr inbounds [200 x i32], ptr @_ZL2yy, i64 0, i64 %40
  %42 = load i32, ptr %41, align 4
  %43 = load i32, ptr %6, align 4
  %44 = add i32 %43, 1
  %45 = mul i32 %42, %44
  %46 = sext i32 %45 to i64
  %47 = load i64, ptr %5, align 8
  %48 = add i64 %47, %46
  store i64 %48, ptr %5, align 8
  br label %49

49:                                               ; preds = %38
  %50 = load i32, ptr %6, align 4
  %51 = add i32 %50, 1
  store i32 %51, ptr %6, align 4
  br label %35, !llvm.loop !9

52:                                               ; preds = %35
  %53 = load i64, ptr %5, align 8
  %54 = icmp eq i64 %53, 59692772
  %55 = zext i1 %54 to i64
  %56 = select i1 %54, i32 0, i32 1
  ret i32 %56
}

; Function Attrs: mustprogress noinline nounwind optnone sspstrong uwtable
define linkonce_odr void @_ZN3IJbIiEclEPKiPiS2_iii(ptr noundef nonnull align 1 dereferenceable(1) %0, ptr noundef %1, ptr noundef %2, ptr noundef %3, i32 noundef %4, i32 noundef %5, i32 noundef %6) #0 comdat align 2 {
  %8 = alloca ptr, align 8
  %9 = alloca ptr, align 8
  %10 = alloca ptr, align 8
  %11 = alloca ptr, align 8
  %12 = alloca i32, align 4
  %13 = alloca i32, align 4
  %14 = alloca i32, align 4
  %15 = alloca i32, align 4
  %16 = alloca i32, align 4
  %17 = alloca i32, align 4
  %18 = alloca i32, align 4
  %19 = alloca i32, align 4
  store ptr %0, ptr %8, align 8
  store ptr %1, ptr %9, align 8
  store ptr %2, ptr %10, align 8
  store ptr %3, ptr %11, align 8
  store i32 %4, ptr %12, align 4
  store i32 %5, ptr %13, align 4
  store i32 %6, ptr %14, align 4
  %20 = load ptr, ptr %8, align 8
  store i32 0, ptr %17, align 4
  br label %21

21:                                               ; preds = %76, %7
  %22 = load i32, ptr %17, align 4
  %23 = load i32, ptr %12, align 4
  %24 = icmp slt i32 %22, %23
  br i1 %24, label %25, label %80

25:                                               ; preds = %21
  %26 = load i32, ptr %17, align 4
  %27 = load i32, ptr %14, align 4
  %28 = add i32 %26, %27
  store i32 %28, ptr %18, align 4
  %29 = load i32, ptr %18, align 4
  %30 = load i32, ptr %12, align 4
  %31 = icmp sgt i32 %29, %30
  br i1 %31, label %32, label %34

32:                                               ; preds = %25
  %33 = load i32, ptr %12, align 4
  store i32 %33, ptr %18, align 4
  br label %34

34:                                               ; preds = %32, %25
  store i32 0, ptr %16, align 4
  br label %35

35:                                               ; preds = %72, %34
  %36 = load i32, ptr %16, align 4
  %37 = load i32, ptr %13, align 4
  %38 = icmp slt i32 %36, %37
  br i1 %38, label %39, label %75

39:                                               ; preds = %35
  %40 = load ptr, ptr %9, align 8
  %41 = load i32, ptr %16, align 4
  %42 = sext i32 %41 to i64
  %43 = getelementptr inbounds i32, ptr %40, i64 %42
  %44 = load i32, ptr %43, align 4
  store i32 %44, ptr %19, align 4
  %45 = load i32, ptr %17, align 4
  store i32 %45, ptr %15, align 4
  br label %46

46:                                               ; preds = %68, %39
  %47 = load i32, ptr %15, align 4
  %48 = load i32, ptr %18, align 4
  %49 = icmp slt i32 %47, %48
  br i1 %49, label %50, label %71

50:                                               ; preds = %46
  %51 = load ptr, ptr %11, align 8
  %52 = load i32, ptr %15, align 4
  %53 = load i32, ptr %13, align 4
  %54 = mul i32 %52, %53
  %55 = load i32, ptr %16, align 4
  %56 = add i32 %54, %55
  %57 = sext i32 %56 to i64
  %58 = getelementptr inbounds i32, ptr %51, i64 %57
  %59 = load i32, ptr %58, align 4
  %60 = load i32, ptr %19, align 4
  %61 = mul i32 %59, %60
  %62 = load ptr, ptr %10, align 8
  %63 = load i32, ptr %15, align 4
  %64 = sext i32 %63 to i64
  %65 = getelementptr inbounds i32, ptr %62, i64 %64
  %66 = load i32, ptr %65, align 4
  %67 = add i32 %66, %61
  store i32 %67, ptr %65, align 4
  br label %68

68:                                               ; preds = %50
  %69 = load i32, ptr %15, align 4
  %70 = add i32 %69, 1
  store i32 %70, ptr %15, align 4
  br label %46, !llvm.loop !10

71:                                               ; preds = %46
  br label %72

72:                                               ; preds = %71
  %73 = load i32, ptr %16, align 4
  %74 = add i32 %73, 1
  store i32 %74, ptr %16, align 4
  br label %35, !llvm.loop !11

75:                                               ; preds = %35
  br label %76

76:                                               ; preds = %75
  %77 = load i32, ptr %14, align 4
  %78 = load i32, ptr %17, align 4
  %79 = add i32 %78, %77
  store i32 %79, ptr %17, align 4
  br label %21, !llvm.loop !12

80:                                               ; preds = %21
  ret void
}

attributes #0 = { mustprogress noinline nounwind optnone sspstrong uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="4" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "zero-call-used-regs"="used-gpr" }
attributes #1 = { mustprogress noinline norecurse nounwind optnone sspstrong uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="4" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 4, !"probe-stack", !"inline-asm"}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 20.1.8"}
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}
!8 = distinct !{!8, !7}
!9 = distinct !{!9, !7}
!10 = distinct !{!10, !7}
!11 = distinct !{!11, !7}
!12 = distinct !{!12, !7}
