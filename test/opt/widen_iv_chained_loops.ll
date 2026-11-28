; The loops continue from the counter of the loop before, so the start of the
; narrow counter is a loop invariant value that the sum of expressions cannot
; cancel against itself. The whole pipeline used to crash in WidenIV. Only the
; compile is checked, the pass output has nothing to pin here.
; RUN: %foffcc %s %t.o

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@iterations = external global i32, align 4

; Function Attrs: nounwind sspstrong uwtable
define void @entry(ptr noundef %0, i8 noundef zeroext %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i8, align 1
  store ptr %0, ptr %3, align 8, !tbaa !10
  store i8 %1, ptr %4, align 1, !tbaa !13
  %5 = load ptr, ptr %3, align 8, !tbaa !10
  %6 = load i8, ptr %4, align 1, !tbaa !13
  call void @fill32(ptr noundef %5, i32 noundef 8388608, i8 noundef zeroext %6)
  ret void
}

; Function Attrs: nounwind sspstrong uwtable
define internal void @fill32(ptr noundef %0, i32 noundef %1, i8 noundef zeroext %2) #0 {
  %4 = alloca ptr, align 8
  %5 = alloca i32, align 4
  %6 = alloca i8, align 1
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  %9 = alloca i32, align 4
  store ptr %0, ptr %4, align 8, !tbaa !10
  store i32 %1, ptr %5, align 4, !tbaa !6
  store i8 %2, ptr %6, align 1, !tbaa !13
  call void @llvm.lifetime.start.p0(ptr %7) #2
  store i32 0, ptr %7, align 4, !tbaa !6
  br label %10

10:                                               ; preds = %110, %3
  %11 = load i32, ptr %7, align 4, !tbaa !6
  %12 = load i32, ptr @iterations, align 4, !tbaa !6
  %13 = icmp slt i32 %11, %12
  br i1 %13, label %15, label %14

14:                                               ; preds = %10
  call void @llvm.lifetime.end.p0(ptr %7) #2
  br label %113

15:                                               ; preds = %10
  call void @llvm.lifetime.start.p0(ptr %8) #2
  store i32 0, ptr %8, align 4, !tbaa !6
  %16 = load i32, ptr %5, align 4, !tbaa !6
  %17 = icmp sgt i32 %16, 128
  br i1 %17, label %18, label %95

18:                                               ; preds = %15
  call void @llvm.lifetime.start.p0(ptr %9) #2
  %19 = load i8, ptr %6, align 1, !tbaa !13
  %20 = zext i8 %19 to i32
  %21 = mul i32 %20, 16843009
  store i32 %21, ptr %9, align 4, !tbaa !6
  br label %22

22:                                               ; preds = %42, %18
  %23 = load i32, ptr %8, align 4, !tbaa !6
  %24 = load i32, ptr %5, align 4, !tbaa !6
  %25 = icmp slt i32 %23, %24
  br i1 %25, label %26, label %34

26:                                               ; preds = %22
  %27 = load ptr, ptr %4, align 8, !tbaa !10
  %28 = ptrtoint ptr %27 to i64
  %29 = load i32, ptr %8, align 4, !tbaa !6
  %30 = sext i32 %29 to i64
  %31 = add i64 %28, %30
  %32 = and i64 %31, 3
  %33 = icmp ne i64 %32, 0
  br label %34

34:                                               ; preds = %26, %22
  %35 = phi i1 [ false, %22 ], [ %33, %26 ]
  br i1 %35, label %36, label %45

36:                                               ; preds = %34
  %37 = load i8, ptr %6, align 1, !tbaa !13
  %38 = load ptr, ptr %4, align 8, !tbaa !10
  %39 = load i32, ptr %8, align 4, !tbaa !6
  %40 = sext i32 %39 to i64
  %41 = getelementptr inbounds i8, ptr %38, i64 %40
  store i8 %37, ptr %41, align 1, !tbaa !13
  br label %42

42:                                               ; preds = %36
  %43 = load i32, ptr %8, align 4, !tbaa !6
  %44 = add i32 %43, 1
  store i32 %44, ptr %8, align 4, !tbaa !6
  br label %22, !llvm.loop !14

45:                                               ; preds = %34
  br label %46

46:                                               ; preds = %76, %45
  %47 = load i32, ptr %8, align 4, !tbaa !6
  %48 = load i32, ptr %5, align 4, !tbaa !6
  %49 = sub i32 %48, 15
  %50 = icmp slt i32 %47, %49
  br i1 %50, label %51, label %79

51:                                               ; preds = %46
  %52 = load i32, ptr %9, align 4, !tbaa !6
  %53 = load ptr, ptr %4, align 8, !tbaa !10
  %54 = load i32, ptr %8, align 4, !tbaa !6
  %55 = sext i32 %54 to i64
  %56 = getelementptr inbounds i8, ptr %53, i64 %55
  %57 = getelementptr inbounds i8, ptr %56, i64 0
  store i32 %52, ptr %57, align 4, !tbaa !6
  %58 = load i32, ptr %9, align 4, !tbaa !6
  %59 = load ptr, ptr %4, align 8, !tbaa !10
  %60 = load i32, ptr %8, align 4, !tbaa !6
  %61 = sext i32 %60 to i64
  %62 = getelementptr inbounds i8, ptr %59, i64 %61
  %63 = getelementptr inbounds i8, ptr %62, i64 4
  store i32 %58, ptr %63, align 4, !tbaa !6
  %64 = load i32, ptr %9, align 4, !tbaa !6
  %65 = load ptr, ptr %4, align 8, !tbaa !10
  %66 = load i32, ptr %8, align 4, !tbaa !6
  %67 = sext i32 %66 to i64
  %68 = getelementptr inbounds i8, ptr %65, i64 %67
  %69 = getelementptr inbounds i8, ptr %68, i64 8
  store i32 %64, ptr %69, align 4, !tbaa !6
  %70 = load i32, ptr %9, align 4, !tbaa !6
  %71 = load ptr, ptr %4, align 8, !tbaa !10
  %72 = load i32, ptr %8, align 4, !tbaa !6
  %73 = sext i32 %72 to i64
  %74 = getelementptr inbounds i8, ptr %71, i64 %73
  %75 = getelementptr inbounds i8, ptr %74, i64 12
  store i32 %70, ptr %75, align 4, !tbaa !6
  br label %76

76:                                               ; preds = %51
  %77 = load i32, ptr %8, align 4, !tbaa !6
  %78 = add i32 %77, 16
  store i32 %78, ptr %8, align 4, !tbaa !6
  br label %46, !llvm.loop !17

79:                                               ; preds = %46
  br label %80

80:                                               ; preds = %91, %79
  %81 = load i32, ptr %8, align 4, !tbaa !6
  %82 = load i32, ptr %5, align 4, !tbaa !6
  %83 = sub i32 %82, 3
  %84 = icmp slt i32 %81, %83
  br i1 %84, label %85, label %94

85:                                               ; preds = %80
  %86 = load i32, ptr %9, align 4, !tbaa !6
  %87 = load ptr, ptr %4, align 8, !tbaa !10
  %88 = load i32, ptr %8, align 4, !tbaa !6
  %89 = sext i32 %88 to i64
  %90 = getelementptr inbounds i8, ptr %87, i64 %89
  store i32 %86, ptr %90, align 4, !tbaa !6
  br label %91

91:                                               ; preds = %85
  %92 = load i32, ptr %8, align 4, !tbaa !6
  %93 = add i32 %92, 4
  store i32 %93, ptr %8, align 4, !tbaa !6
  br label %80, !llvm.loop !18

94:                                               ; preds = %80
  call void @llvm.lifetime.end.p0(ptr %9) #2
  br label %95

95:                                               ; preds = %94, %15
  br label %96

96:                                               ; preds = %106, %95
  %97 = load i32, ptr %8, align 4, !tbaa !6
  %98 = load i32, ptr %5, align 4, !tbaa !6
  %99 = icmp slt i32 %97, %98
  br i1 %99, label %100, label %109

100:                                              ; preds = %96
  %101 = load i8, ptr %6, align 1, !tbaa !13
  %102 = load ptr, ptr %4, align 8, !tbaa !10
  %103 = load i32, ptr %8, align 4, !tbaa !6
  %104 = sext i32 %103 to i64
  %105 = getelementptr inbounds i8, ptr %102, i64 %104
  store i8 %101, ptr %105, align 1, !tbaa !13
  br label %106

106:                                              ; preds = %100
  %107 = load i32, ptr %8, align 4, !tbaa !6
  %108 = add i32 %107, 1
  store i32 %108, ptr %8, align 4, !tbaa !6
  br label %96, !llvm.loop !19

109:                                              ; preds = %96
  call void @llvm.lifetime.end.p0(ptr %8) #2
  br label %110

110:                                              ; preds = %109
  %111 = load i32, ptr %7, align 4, !tbaa !6
  %112 = add i32 %111, 1
  store i32 %112, ptr %7, align 4, !tbaa !6
  br label %10, !llvm.loop !20

113:                                              ; preds = %14
  ret void
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(ptr captures(none)) #1

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(ptr captures(none)) #1

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
!11 = !{!"p1 omnipotent char", !12, i64 0}
!12 = !{!"any pointer", !8, i64 0}
!13 = !{!8, !8, i64 0}
!14 = distinct !{!14, !15, !16}
!15 = !{!"llvm.loop.mustprogress"}
!16 = !{!"llvm.loop.unroll.disable"}
!17 = distinct !{!17, !15, !16}
!18 = distinct !{!18, !15, !16}
!19 = distinct !{!19, !15, !16}
!20 = distinct !{!20, !15, !16}
