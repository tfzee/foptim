; RUN: %foffcc %s --print-fir --passes "pipeline:default_release_early,pipeline:default_release_middle,PartialInlineSplit" | FileCheck %s

; No call site passes a known value for the guarded argument, so folding the
; guard is not possible and big stays whole.

; CHECK-NOT: big.rest
; CHECK-LABEL: func big<
; CHECK-NOT: OUTLINED

; ModuleID = '/tmp/claude-1000/-home-tim-programming-foptim/9208e6f0-c7c6-4a6f-88b1-c6e6a2f73eee/scratchpad/pn.c'
source_filename = "/tmp/claude-1000/-home-tim-programming-foptim/9208e6f0-c7c6-4a6f-88b1-c6e6a2f73eee/scratchpad/pn.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: nounwind sspstrong uwtable
define i64 @big(ptr noundef %0, i64 noundef %1, ptr noundef %2) #0 {
  %4 = alloca i64, align 8
  %5 = alloca ptr, align 8
  %6 = alloca i64, align 8
  %7 = alloca ptr, align 8
  %8 = alloca i64, align 8
  %9 = alloca i64, align 8
  store ptr %0, ptr %5, align 8, !tbaa !10
  store i64 %1, ptr %6, align 8, !tbaa !13
  store ptr %2, ptr %7, align 8, !tbaa !10
  %10 = load i64, ptr %6, align 8, !tbaa !13
  %11 = icmp eq i64 %10, 0
  br i1 %11, label %12, label %13

12:                                               ; preds = %3
  store i64 -1, ptr %4, align 8
  br label %100

13:                                               ; preds = %3
  call void @llvm.lifetime.start.p0(ptr %8) #2
  store i64 0, ptr %8, align 8, !tbaa !13
  call void @llvm.lifetime.start.p0(ptr %9) #2
  store i64 0, ptr %9, align 8, !tbaa !13
  br label %14

14:                                               ; preds = %53, %13
  %15 = load i64, ptr %9, align 8, !tbaa !13
  %16 = load i64, ptr %6, align 8, !tbaa !13
  %17 = icmp slt i64 %15, %16
  br i1 %17, label %19, label %18

18:                                               ; preds = %14
  call void @llvm.lifetime.end.p0(ptr %9) #2
  br label %56

19:                                               ; preds = %14
  %20 = load ptr, ptr %5, align 8, !tbaa !10
  %21 = load i64, ptr %9, align 8, !tbaa !13
  %22 = getelementptr inbounds i64, ptr %20, i64 %21
  %23 = load i64, ptr %22, align 8, !tbaa !13
  %24 = load ptr, ptr %7, align 8, !tbaa !10
  %25 = load i64, ptr %9, align 8, !tbaa !13
  %26 = getelementptr inbounds i64, ptr %24, i64 %25
  %27 = load i64, ptr %26, align 8, !tbaa !13
  %28 = mul i64 %23, %27
  %29 = load i64, ptr %8, align 8, !tbaa !13
  %30 = add i64 %29, %28
  store i64 %30, ptr %8, align 8, !tbaa !13
  %31 = load i64, ptr %8, align 8, !tbaa !13
  %32 = load i64, ptr %9, align 8, !tbaa !13
  %33 = xor i64 %31, %32
  %34 = load ptr, ptr %7, align 8, !tbaa !10
  %35 = load i64, ptr %9, align 8, !tbaa !13
  %36 = getelementptr inbounds i64, ptr %34, i64 %35
  store i64 %33, ptr %36, align 8, !tbaa !13
  %37 = load i64, ptr %8, align 8, !tbaa !13
  %38 = load i64, ptr %9, align 8, !tbaa !13
  %39 = mul i64 3, %38
  %40 = add i64 %37, %39
  %41 = load ptr, ptr %5, align 8, !tbaa !10
  %42 = load i64, ptr %9, align 8, !tbaa !13
  %43 = getelementptr inbounds i64, ptr %41, i64 %42
  store i64 %40, ptr %43, align 8, !tbaa !13
  %44 = load i64, ptr %8, align 8, !tbaa !13
  %45 = icmp sgt i64 %44, 1000
  br i1 %45, label %46, label %52

46:                                               ; preds = %19
  %47 = load ptr, ptr %5, align 8, !tbaa !10
  %48 = getelementptr inbounds i64, ptr %47, i64 0
  %49 = load i64, ptr %48, align 8, !tbaa !13
  %50 = load i64, ptr %8, align 8, !tbaa !13
  %51 = sub i64 %50, %49
  store i64 %51, ptr %8, align 8, !tbaa !13
  br label %52

52:                                               ; preds = %46, %19
  br label %53

53:                                               ; preds = %52
  %54 = load i64, ptr %9, align 8, !tbaa !13
  %55 = add i64 %54, 1
  store i64 %55, ptr %9, align 8, !tbaa !13
  br label %14, !llvm.loop !15

56:                                               ; preds = %18
  %57 = load ptr, ptr %5, align 8, !tbaa !10
  %58 = load i64, ptr %6, align 8, !tbaa !13
  %59 = sdiv i64 %58, 2
  %60 = getelementptr inbounds i64, ptr %57, i64 %59
  %61 = load i64, ptr %60, align 8, !tbaa !13
  %62 = mul i64 %61, 5
  %63 = load ptr, ptr %7, align 8, !tbaa !10
  %64 = getelementptr inbounds i64, ptr %63, i64 0
  %65 = load i64, ptr %64, align 8, !tbaa !13
  %66 = add i64 %62, %65
  %67 = load i64, ptr %8, align 8, !tbaa !13
  %68 = add i64 %67, %66
  store i64 %68, ptr %8, align 8, !tbaa !13
  %69 = load i64, ptr %8, align 8, !tbaa !13
  %70 = ashr i64 %69, 3
  %71 = load i64, ptr %8, align 8, !tbaa !13
  %72 = xor i64 %71, %70
  store i64 %72, ptr %8, align 8, !tbaa !13
  %73 = load ptr, ptr %7, align 8, !tbaa !10
  %74 = load i64, ptr %6, align 8, !tbaa !13
  %75 = sub i64 %74, 1
  %76 = getelementptr inbounds i64, ptr %73, i64 %75
  %77 = load i64, ptr %76, align 8, !tbaa !13
  %78 = load ptr, ptr %7, align 8, !tbaa !10
  %79 = getelementptr inbounds i64, ptr %78, i64 1
  %80 = load i64, ptr %79, align 8, !tbaa !13
  %81 = mul i64 %77, %80
  %82 = load i64, ptr %8, align 8, !tbaa !13
  %83 = add i64 %82, %81
  store i64 %83, ptr %8, align 8, !tbaa !13
  %84 = load i64, ptr %8, align 8, !tbaa !13
  %85 = load ptr, ptr %7, align 8, !tbaa !10
  %86 = getelementptr inbounds i64, ptr %85, i64 2
  store i64 %84, ptr %86, align 8, !tbaa !13
  %87 = load i64, ptr %8, align 8, !tbaa !13
  %88 = load ptr, ptr %5, align 8, !tbaa !10
  %89 = getelementptr inbounds i64, ptr %88, i64 1
  %90 = load i64, ptr %89, align 8, !tbaa !13
  %91 = add i64 %90, %87
  store i64 %91, ptr %89, align 8, !tbaa !13
  %92 = load ptr, ptr %5, align 8, !tbaa !10
  %93 = getelementptr inbounds i64, ptr %92, i64 2
  %94 = load i64, ptr %93, align 8, !tbaa !13
  %95 = load ptr, ptr %7, align 8, !tbaa !10
  %96 = getelementptr inbounds i64, ptr %95, i64 3
  %97 = load i64, ptr %96, align 8, !tbaa !13
  %98 = add i64 %97, %94
  store i64 %98, ptr %96, align 8, !tbaa !13
  %99 = load i64, ptr %8, align 8, !tbaa !13
  store i64 %99, ptr %4, align 8
  call void @llvm.lifetime.end.p0(ptr %8) #2
  br label %100

100:                                              ; preds = %56, %12
  %101 = load i64, ptr %4, align 8
  ret i64 %101
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(ptr captures(none)) #1

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(ptr captures(none)) #1

; Function Attrs: nounwind sspstrong uwtable
define i64 @user2(ptr noundef %0, ptr noundef %1, i64 noundef %2) #0 {
  %4 = alloca ptr, align 8
  %5 = alloca ptr, align 8
  %6 = alloca i64, align 8
  store ptr %0, ptr %4, align 8, !tbaa !10
  store ptr %1, ptr %5, align 8, !tbaa !10
  store i64 %2, ptr %6, align 8, !tbaa !13
  %7 = load ptr, ptr %4, align 8, !tbaa !10
  %8 = load i64, ptr %6, align 8, !tbaa !13
  %9 = load ptr, ptr %5, align 8, !tbaa !10
  %10 = call i64 @big(ptr noundef %7, i64 noundef %8, ptr noundef %9)
  %11 = load ptr, ptr %5, align 8, !tbaa !10
  %12 = load i64, ptr %6, align 8, !tbaa !13
  %13 = add i64 %12, 1
  %14 = load ptr, ptr %4, align 8, !tbaa !10
  %15 = call i64 @big(ptr noundef %11, i64 noundef %13, ptr noundef %14)
  %16 = add i64 %10, %15
  ret i64 %16
}

; Function Attrs: nounwind sspstrong uwtable
define i64 @user3(ptr noundef %0, ptr noundef %1, i64 noundef %2) #0 {
  %4 = alloca ptr, align 8
  %5 = alloca ptr, align 8
  %6 = alloca i64, align 8
  store ptr %0, ptr %4, align 8, !tbaa !10
  store ptr %1, ptr %5, align 8, !tbaa !10
  store i64 %2, ptr %6, align 8, !tbaa !13
  %7 = load ptr, ptr %4, align 8, !tbaa !10
  %8 = load i64, ptr %6, align 8, !tbaa !13
  %9 = load ptr, ptr %5, align 8, !tbaa !10
  %10 = call i64 @big(ptr noundef %7, i64 noundef %8, ptr noundef %9)
  %11 = load ptr, ptr %5, align 8, !tbaa !10
  %12 = load i64, ptr %6, align 8, !tbaa !13
  %13 = load ptr, ptr %4, align 8, !tbaa !10
  %14 = call i64 @big(ptr noundef %11, i64 noundef %12, ptr noundef %13)
  %15 = mul i64 %10, %14
  ret i64 %15
}

attributes #0 = { nounwind sspstrong uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="4" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "zero-call-used-regs"="used-gpr" }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { nounwind }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}
!llvm.errno.tbaa = !{!6}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 4, !"probe-stack", !"inline-asm"}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 22.1.8"}
!6 = !{!7, !7, i64 0}
!7 = !{!"int", !8, i64 0}
!8 = !{!"omnipotent char", !9, i64 0}
!9 = !{!"Simple C/C++ TBAA"}
!10 = !{!11, !11, i64 0}
!11 = !{!"p1 long", !12, i64 0}
!12 = !{!"any pointer", !8, i64 0}
!13 = !{!14, !14, i64 0}
!14 = !{!"long", !8, i64 0}
!15 = distinct !{!15, !16, !17}
!16 = !{!"llvm.loop.mustprogress"}
!17 = !{!"llvm.loop.unroll.disable"}
