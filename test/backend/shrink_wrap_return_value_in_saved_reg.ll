; RUN: %foffcc %s --print-mir --no-reorder-funcs | FileCheck %s
; Handwritten CHECKs: only the order at the exit of checkTreePage (from sqlite)
; is pinned. The function is shrink wrapped and has static allocas, so the pops
; go in front of the add rsp of the allocas. The return value lives in rbx and
; has to be moved to eax before rbx is popped.
; CHECK-LABEL: func checkTreePage
; CHECK: $eax: i32 = $ebx: i32
; CHECK-NEXT: pop($r12: i64, )
; CHECK-NEXT: pop($r13: i64, )
; CHECK-NEXT: pop($r14: i64, )
; CHECK-NEXT: pop($r15: i64, )
; CHECK-NEXT: pop($rbx: i64, )
; CHECK-NEXT: $rsp: i64 += 48: i8
; ModuleID = 'sqlite'
source_filename = "sqlite"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%struct.CellInfo = type { i64, ptr, i32, i16, i16 }
%struct.IntegrityCk = type { ptr, ptr, ptr, i32, i32, i32, i32, i32, i8, ptr, i32, i32, i32, %struct.sqlite3_str, ptr, ptr, i64 }
%struct.sqlite3_str = type { ptr, ptr, i32, i32, i32, i8, i8 }
%struct.BtShared = type { ptr, ptr, ptr, ptr, i8, i8, i8, i8, i8, i8, i8, i16, i16, i16, i16, i16, i32, i32, i32, i32, ptr, ptr, ptr, ptr, ptr, i32 }
%struct.MemPage = type { i8, i8, i8, i32, i8, i8, i8, i8, i8, i16, i16, i16, i32, i16, i16, [4 x i16], [4 x ptr], ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr }

@.str.455 = external unnamed_addr constant [18 x i8], align 1
@.str.456 = external unnamed_addr constant [38 x i8], align 1
@.str.457 = external unnamed_addr constant [23 x i8], align 1
@.str.458 = external unnamed_addr constant [38 x i8], align 1
@.str.459 = external unnamed_addr constant [22 x i8], align 1
@.str.460 = external unnamed_addr constant [26 x i8], align 1
@.str.461 = external unnamed_addr constant [30 x i8], align 1
@.str.462 = external unnamed_addr constant [30 x i8], align 1
@.str.463 = external unnamed_addr constant [24 x i8], align 1
@.str.464 = external unnamed_addr constant [21 x i8], align 1
@.str.465 = external unnamed_addr constant [24 x i8], align 1
@.str.466 = external unnamed_addr constant [25 x i8], align 1
@.str.467 = external unnamed_addr constant [37 x i8], align 1
@.str.468 = external unnamed_addr constant [52 x i8], align 1

; Function Attrs: noinline nounwind optnone sspstrong uwtable
define i32 @sqlite3Get4byte(ptr noundef %0) #0 {
  %2 = alloca ptr, align 8
  store ptr %0, ptr %2, align 8
  %3 = load ptr, ptr %2, align 8
  %4 = getelementptr inbounds i8, ptr %3, i64 0
  %5 = load i8, ptr %4, align 1
  %6 = zext i8 %5 to i32
  %7 = shl i32 %6, 24
  %8 = load ptr, ptr %2, align 8
  %9 = getelementptr inbounds i8, ptr %8, i64 1
  %10 = load i8, ptr %9, align 1
  %11 = zext i8 %10 to i32
  %12 = shl i32 %11, 16
  %13 = or i32 %7, %12
  %14 = load ptr, ptr %2, align 8
  %15 = getelementptr inbounds i8, ptr %14, i64 2
  %16 = load i8, ptr %15, align 1
  %17 = zext i8 %16 to i32
  %18 = shl i32 %17, 8
  %19 = or i32 %13, %18
  %20 = load ptr, ptr %2, align 8
  %21 = getelementptr inbounds i8, ptr %20, i64 3
  %22 = load i8, ptr %21, align 1
  %23 = zext i8 %22 to i32
  %24 = or i32 %19, %23
  ret i32 %24
}

; Function Attrs: noinline nounwind optnone sspstrong uwtable
declare i32 @btreeGetPage(ptr noundef, i32 noundef, ptr noundef, i32 noundef) #0

; Function Attrs: noinline nounwind optnone sspstrong uwtable
declare void @releasePage(ptr noundef) #0

; Function Attrs: noinline nounwind optnone sspstrong uwtable
declare i32 @btreeInitPage(ptr noundef) #0

; Function Attrs: noinline nounwind optnone sspstrong uwtable
declare i32 @btreeComputeFreeSpace(ptr noundef) #0

; Function Attrs: noinline nounwind optnone sspstrong uwtable
declare void @checkList(ptr noundef, i32 noundef, i32 noundef, i32 noundef) #0

; Function Attrs: noinline nounwind optnone sspstrong uwtable
declare void @checkAppendMsg(ptr noundef, ptr noundef, ...) #0

; Function Attrs: noinline nounwind optnone sspstrong uwtable
declare void @checkPtrmap(ptr noundef, i32 noundef, i8 noundef zeroext, i32 noundef) #0

; Function Attrs: noinline nounwind optnone sspstrong uwtable
define i32 @checkTreePage(ptr noundef %0, i32 noundef %1, ptr noundef %2, i64 noundef %3) #0 {
  %5 = alloca i32, align 4
  %6 = alloca ptr, align 8
  %7 = alloca i32, align 4
  %8 = alloca ptr, align 8
  %9 = alloca i64, align 8
  %10 = alloca ptr, align 8
  %11 = alloca i32, align 4
  %12 = alloca i32, align 4
  %13 = alloca i32, align 4
  %14 = alloca i32, align 4
  %15 = alloca i32, align 4
  %16 = alloca i32, align 4
  %17 = alloca i32, align 4
  %18 = alloca i32, align 4
  %19 = alloca i32, align 4
  %20 = alloca i32, align 4
  %21 = alloca i32, align 4
  %22 = alloca ptr, align 8
  %23 = alloca ptr, align 8
  %24 = alloca ptr, align 8
  %25 = alloca ptr, align 8
  %26 = alloca i32, align 4
  %27 = alloca i32, align 4
  %28 = alloca i32, align 4
  %29 = alloca ptr, align 8
  %30 = alloca i32, align 4
  %31 = alloca i32, align 4
  %32 = alloca ptr, align 8
  %33 = alloca i32, align 4
  %34 = alloca i32, align 4
  %35 = alloca i8, align 1
  %36 = alloca %struct.CellInfo, align 8
  %37 = alloca i32, align 4
  %38 = alloca i32, align 4
  %39 = alloca i32, align 4
  %40 = alloca i32, align 4
  %41 = alloca i32, align 4
  store ptr %0, ptr %6, align 8
  store i32 %1, ptr %7, align 4
  store ptr %2, ptr %8, align 8
  store i64 %3, ptr %9, align 8
  store ptr null, ptr %10, align 8
  store i32 -1, ptr %13, align 4
  store i32 1, ptr %20, align 4
  store i32 1, ptr %21, align 4
  store ptr null, ptr %29, align 8
  store i32 0, ptr %31, align 4
  %42 = load ptr, ptr %6, align 8
  %43 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %42, i32 0, i32 9
  %44 = load ptr, ptr %43, align 8
  store ptr %44, ptr %32, align 8
  %45 = load ptr, ptr %6, align 8
  %46 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %45, i32 0, i32 11
  %47 = load i32, ptr %46, align 4
  store i32 %47, ptr %33, align 4
  %48 = load ptr, ptr %6, align 8
  %49 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %48, i32 0, i32 12
  %50 = load i32, ptr %49, align 8
  store i32 %50, ptr %34, align 4
  store i8 0, ptr %35, align 1
  %51 = load ptr, ptr %6, align 8
  call void @checkProgress(ptr noundef %51)
  %52 = load ptr, ptr %6, align 8
  %53 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %52, i32 0, i32 4
  %54 = load i32, ptr %53, align 4
  %55 = icmp eq i32 %54, 0
  br i1 %55, label %56, label %57

56:                                               ; preds = %4
  store i32 0, ptr %5, align 4
  br label %649

57:                                               ; preds = %4
  %58 = load ptr, ptr %6, align 8
  %59 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %58, i32 0, i32 0
  %60 = load ptr, ptr %59, align 8
  store ptr %60, ptr %25, align 8
  %61 = load ptr, ptr %25, align 8
  %62 = getelementptr inbounds nuw %struct.BtShared, ptr %61, i32 0, i32 17
  %63 = load i32, ptr %62, align 8
  store i32 %63, ptr %27, align 4
  %64 = load i32, ptr %7, align 4
  %65 = icmp eq i32 %64, 0
  br i1 %65, label %66, label %67

66:                                               ; preds = %57
  store i32 0, ptr %5, align 4
  br label %649

67:                                               ; preds = %57
  %68 = load ptr, ptr %6, align 8
  %69 = load i32, ptr %7, align 4
  %70 = call i32 @checkRef(ptr noundef %68, i32 noundef %69)
  %71 = icmp ne i32 %70, 0
  br i1 %71, label %72, label %73

72:                                               ; preds = %67
  store i32 0, ptr %5, align 4
  br label %649

73:                                               ; preds = %67
  %74 = load ptr, ptr %6, align 8
  %75 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %74, i32 0, i32 9
  store ptr @.str.455, ptr %75, align 8
  %76 = load i32, ptr %7, align 4
  %77 = load ptr, ptr %6, align 8
  %78 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %77, i32 0, i32 11
  store i32 %76, ptr %78, align 4
  %79 = load ptr, ptr %6, align 8
  %80 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %79, i32 0, i32 8
  %81 = load i8, ptr %80, align 4
  %82 = add i8 %81, 1
  store i8 %82, ptr %80, align 4
  %83 = load ptr, ptr %25, align 8
  %84 = load i32, ptr %7, align 4
  %85 = call i32 @btreeGetPage(ptr noundef %83, i32 noundef %84, ptr noundef %10, i32 noundef 0)
  store i32 %85, ptr %12, align 4
  %86 = icmp ne i32 %85, 0
  br i1 %86, label %87, label %96

87:                                               ; preds = %73
  %88 = load ptr, ptr %6, align 8
  %89 = load i32, ptr %12, align 4
  call void (ptr, ptr, ...) @checkAppendMsg(ptr noundef %88, ptr noundef @.str.456, i32 noundef %89)
  %90 = load i32, ptr %12, align 4
  %91 = icmp eq i32 %90, 3082
  br i1 %91, label %92, label %95

92:                                               ; preds = %87
  %93 = load ptr, ptr %6, align 8
  %94 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %93, i32 0, i32 6
  store i32 7, ptr %94, align 4
  br label %95

95:                                               ; preds = %92, %87
  br label %625

96:                                               ; preds = %73
  %97 = load ptr, ptr %6, align 8
  %98 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %97, i32 0, i32 8
  %99 = load i8, ptr %98, align 4
  %100 = zext i8 %99 to i32
  %101 = icmp sgt i32 %100, 20
  br i1 %101, label %102, label %104

102:                                              ; preds = %96
  %103 = load ptr, ptr %6, align 8
  call void (ptr, ptr, ...) @checkAppendMsg(ptr noundef %103, ptr noundef @.str.457, i32 noundef 20)
  br label %625

104:                                              ; preds = %96
  %105 = load ptr, ptr %10, align 8
  %106 = getelementptr inbounds nuw %struct.MemPage, ptr %105, i32 0, i32 0
  %107 = load i8, ptr %106, align 8
  store i8 %107, ptr %35, align 1
  %108 = load ptr, ptr %10, align 8
  %109 = getelementptr inbounds nuw %struct.MemPage, ptr %108, i32 0, i32 0
  store i8 0, ptr %109, align 8
  %110 = load ptr, ptr %10, align 8
  %111 = call i32 @btreeInitPage(ptr noundef %110)
  store i32 %111, ptr %12, align 4
  %112 = icmp ne i32 %111, 0
  br i1 %112, label %113, label %116

113:                                              ; preds = %104
  %114 = load ptr, ptr %6, align 8
  %115 = load i32, ptr %12, align 4
  call void (ptr, ptr, ...) @checkAppendMsg(ptr noundef %114, ptr noundef @.str.458, i32 noundef %115)
  br label %625

116:                                              ; preds = %104
  %117 = load ptr, ptr %10, align 8
  %118 = call i32 @btreeComputeFreeSpace(ptr noundef %117)
  store i32 %118, ptr %12, align 4
  %119 = icmp ne i32 %118, 0
  br i1 %119, label %120, label %123

120:                                              ; preds = %116
  %121 = load ptr, ptr %6, align 8
  %122 = load i32, ptr %12, align 4
  call void (ptr, ptr, ...) @checkAppendMsg(ptr noundef %121, ptr noundef @.str.459, i32 noundef %122)
  br label %625

123:                                              ; preds = %116
  %124 = load ptr, ptr %10, align 8
  %125 = getelementptr inbounds nuw %struct.MemPage, ptr %124, i32 0, i32 18
  %126 = load ptr, ptr %125, align 8
  store ptr %126, ptr %22, align 8
  %127 = load ptr, ptr %10, align 8
  %128 = getelementptr inbounds nuw %struct.MemPage, ptr %127, i32 0, i32 5
  %129 = load i8, ptr %128, align 1
  %130 = zext i8 %129 to i32
  store i32 %130, ptr %17, align 4
  %131 = load ptr, ptr %6, align 8
  %132 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %131, i32 0, i32 9
  store ptr @.str.460, ptr %132, align 8
  %133 = load ptr, ptr %22, align 8
  %134 = load i32, ptr %17, align 4
  %135 = add i32 %134, 5
  %136 = sext i32 %135 to i64
  %137 = getelementptr inbounds i8, ptr %133, i64 %136
  %138 = getelementptr inbounds i8, ptr %137, i64 0
  %139 = load i8, ptr %138, align 1
  %140 = zext i8 %139 to i32
  %141 = shl i32 %140, 8
  %142 = load ptr, ptr %22, align 8
  %143 = load i32, ptr %17, align 4
  %144 = add i32 %143, 5
  %145 = sext i32 %144 to i64
  %146 = getelementptr inbounds i8, ptr %142, i64 %145
  %147 = getelementptr inbounds i8, ptr %146, i64 1
  %148 = load i8, ptr %147, align 1
  %149 = zext i8 %148 to i32
  %150 = or i32 %141, %149
  %151 = sub i32 %150, 1
  %152 = and i32 %151, 65535
  %153 = add i32 %152, 1
  store i32 %153, ptr %28, align 4
  %154 = load ptr, ptr %22, align 8
  %155 = load i32, ptr %17, align 4
  %156 = add i32 %155, 3
  %157 = sext i32 %156 to i64
  %158 = getelementptr inbounds i8, ptr %154, i64 %157
  %159 = getelementptr inbounds i8, ptr %158, i64 0
  %160 = load i8, ptr %159, align 1
  %161 = zext i8 %160 to i32
  %162 = shl i32 %161, 8
  %163 = load ptr, ptr %22, align 8
  %164 = load i32, ptr %17, align 4
  %165 = add i32 %164, 3
  %166 = sext i32 %165 to i64
  %167 = getelementptr inbounds i8, ptr %163, i64 %166
  %168 = getelementptr inbounds i8, ptr %167, i64 1
  %169 = load i8, ptr %168, align 1
  %170 = zext i8 %169 to i32
  %171 = or i32 %162, %170
  store i32 %171, ptr %19, align 4
  %172 = load ptr, ptr %10, align 8
  %173 = getelementptr inbounds nuw %struct.MemPage, ptr %172, i32 0, i32 4
  %174 = load i8, ptr %173, align 8
  %175 = zext i8 %174 to i32
  %176 = icmp ne i32 %175, 0
  br i1 %176, label %183, label %177

177:                                              ; preds = %123
  %178 = load ptr, ptr %10, align 8
  %179 = getelementptr inbounds nuw %struct.MemPage, ptr %178, i32 0, i32 1
  %180 = load i8, ptr %179, align 1
  %181 = zext i8 %180 to i32
  %182 = icmp eq i32 %181, 0
  br i1 %182, label %183, label %190

183:                                              ; preds = %177, %123
  %184 = load i32, ptr %19, align 4
  %185 = sext i32 %184 to i64
  %186 = load ptr, ptr %6, align 8
  %187 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %186, i32 0, i32 16
  %188 = load i64, ptr %187, align 8
  %189 = add i64 %188, %185
  store i64 %189, ptr %187, align 8
  br label %190

190:                                              ; preds = %183, %177
  %191 = load i32, ptr %17, align 4
  %192 = add i32 %191, 12
  %193 = load ptr, ptr %10, align 8
  %194 = getelementptr inbounds nuw %struct.MemPage, ptr %193, i32 0, i32 4
  %195 = load i8, ptr %194, align 8
  %196 = zext i8 %195 to i32
  %197 = mul i32 4, %196
  %198 = sub i32 %192, %197
  store i32 %198, ptr %18, align 4
  %199 = load ptr, ptr %22, align 8
  %200 = load i32, ptr %18, align 4
  %201 = load i32, ptr %19, align 4
  %202 = sub i32 %201, 1
  %203 = mul i32 2, %202
  %204 = add i32 %200, %203
  %205 = sext i32 %204 to i64
  %206 = getelementptr inbounds i8, ptr %199, i64 %205
  store ptr %206, ptr %24, align 8
  %207 = load ptr, ptr %10, align 8
  %208 = getelementptr inbounds nuw %struct.MemPage, ptr %207, i32 0, i32 4
  %209 = load i8, ptr %208, align 8
  %210 = icmp ne i8 %209, 0
  br i1 %210, label %233, label %211

211:                                              ; preds = %190
  %212 = load ptr, ptr %22, align 8
  %213 = load i32, ptr %17, align 4
  %214 = add i32 %213, 8
  %215 = sext i32 %214 to i64
  %216 = getelementptr inbounds i8, ptr %212, i64 %215
  %217 = call i32 @sqlite3Get4byte(ptr noundef %216)
  store i32 %217, ptr %15, align 4
  %218 = load ptr, ptr %25, align 8
  %219 = getelementptr inbounds nuw %struct.BtShared, ptr %218, i32 0, i32 5
  %220 = load i8, ptr %219, align 1
  %221 = icmp ne i8 %220, 0
  br i1 %221, label %222, label %228

222:                                              ; preds = %211
  %223 = load ptr, ptr %6, align 8
  %224 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %223, i32 0, i32 9
  store ptr @.str.461, ptr %224, align 8
  %225 = load ptr, ptr %6, align 8
  %226 = load i32, ptr %15, align 4
  %227 = load i32, ptr %7, align 4
  call void @checkPtrmap(ptr noundef %225, i32 noundef %226, i8 noundef zeroext 5, i32 noundef %227)
  br label %228

228:                                              ; preds = %222, %211
  %229 = load ptr, ptr %6, align 8
  %230 = load i32, ptr %15, align 4
  %231 = load i64, ptr %9, align 8
  %232 = call i32 @checkTreePage(ptr noundef %229, i32 noundef %230, ptr noundef %9, i64 noundef %231)
  store i32 %232, ptr %13, align 4
  store i32 0, ptr %21, align 4
  br label %239

233:                                              ; preds = %190
  %234 = load ptr, ptr %6, align 8
  %235 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %234, i32 0, i32 14
  %236 = load ptr, ptr %235, align 8
  store ptr %236, ptr %29, align 8
  %237 = load ptr, ptr %29, align 8
  %238 = getelementptr inbounds i32, ptr %237, i64 0
  store i32 0, ptr %238, align 4
  br label %239

239:                                              ; preds = %233, %228
  %240 = load i32, ptr %19, align 4
  %241 = sub i32 %240, 1
  store i32 %241, ptr %11, align 4
  br label %242

242:                                              ; preds = %419, %239
  %243 = load i32, ptr %11, align 4
  %244 = icmp sge i32 %243, 0
  br i1 %244, label %245, label %250

245:                                              ; preds = %242
  %246 = load ptr, ptr %6, align 8
  %247 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %246, i32 0, i32 4
  %248 = load i32, ptr %247, align 4
  %249 = icmp ne i32 %248, 0
  br label %250

250:                                              ; preds = %245, %242
  %251 = phi i1 [ false, %242 ], [ %249, %245 ]
  br i1 %251, label %252, label %422

252:                                              ; preds = %250
  %253 = load i32, ptr %11, align 4
  %254 = load ptr, ptr %6, align 8
  %255 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %254, i32 0, i32 12
  store i32 %253, ptr %255, align 8
  %256 = load ptr, ptr %24, align 8
  %257 = getelementptr inbounds i8, ptr %256, i64 0
  %258 = load i8, ptr %257, align 1
  %259 = zext i8 %258 to i32
  %260 = shl i32 %259, 8
  %261 = load ptr, ptr %24, align 8
  %262 = getelementptr inbounds i8, ptr %261, i64 1
  %263 = load i8, ptr %262, align 1
  %264 = zext i8 %263 to i32
  %265 = or i32 %260, %264
  store i32 %265, ptr %26, align 4
  %266 = load ptr, ptr %24, align 8
  %267 = getelementptr inbounds i8, ptr %266, i64 -2
  store ptr %267, ptr %24, align 8
  %268 = load i32, ptr %26, align 4
  %269 = load i32, ptr %28, align 4
  %270 = icmp ult i32 %268, %269
  br i1 %270, label %276, label %271

271:                                              ; preds = %252
  %272 = load i32, ptr %26, align 4
  %273 = load i32, ptr %27, align 4
  %274 = sub i32 %273, 4
  %275 = icmp ugt i32 %272, %274
  br i1 %275, label %276, label %282

276:                                              ; preds = %271, %252
  %277 = load ptr, ptr %6, align 8
  %278 = load i32, ptr %26, align 4
  %279 = load i32, ptr %28, align 4
  %280 = load i32, ptr %27, align 4
  %281 = sub i32 %280, 4
  call void (ptr, ptr, ...) @checkAppendMsg(ptr noundef %277, ptr noundef @.str.462, i32 noundef %278, i32 noundef %279, i32 noundef %281)
  store i32 0, ptr %20, align 4
  br label %419

282:                                              ; preds = %271
  %283 = load ptr, ptr %22, align 8
  %284 = load i32, ptr %26, align 4
  %285 = zext i32 %284 to i64
  %286 = getelementptr inbounds nuw i8, ptr %283, i64 %285
  store ptr %286, ptr %23, align 8
  %287 = load ptr, ptr %10, align 8
  %288 = getelementptr inbounds nuw %struct.MemPage, ptr %287, i32 0, i32 24
  %289 = load ptr, ptr %288, align 8
  %290 = load ptr, ptr %10, align 8
  %291 = load ptr, ptr %23, align 8
  call void %289(ptr noundef %290, ptr noundef %291, ptr noundef %36)
  %292 = load i32, ptr %26, align 4
  %293 = getelementptr inbounds nuw %struct.CellInfo, ptr %36, i32 0, i32 4
  %294 = load i16, ptr %293, align 2
  %295 = zext i16 %294 to i32
  %296 = add i32 %292, %295
  %297 = load i32, ptr %27, align 4
  %298 = icmp ugt i32 %296, %297
  br i1 %298, label %299, label %301

299:                                              ; preds = %282
  %300 = load ptr, ptr %6, align 8
  call void (ptr, ptr, ...) @checkAppendMsg(ptr noundef %300, ptr noundef @.str.463)
  store i32 0, ptr %20, align 4
  br label %419

301:                                              ; preds = %282
  %302 = getelementptr inbounds nuw %struct.CellInfo, ptr %36, i32 0, i32 2
  %303 = load i32, ptr %302, align 8
  %304 = icmp ne i32 %303, 0
  br i1 %304, label %305, label %314

305:                                              ; preds = %301
  %306 = getelementptr inbounds nuw %struct.CellInfo, ptr %36, i32 0, i32 1
  %307 = load ptr, ptr %306, align 8
  %308 = getelementptr inbounds i8, ptr %307, i64 0
  %309 = load i8, ptr %308, align 1
  %310 = zext i8 %309 to i32
  %311 = icmp slt i32 %310, 2
  br i1 %311, label %312, label %314

312:                                              ; preds = %305
  %313 = load ptr, ptr %6, align 8
  call void (ptr, ptr, ...) @checkAppendMsg(ptr noundef %313, ptr noundef @.str.464)
  store i32 0, ptr %20, align 4
  br label %419

314:                                              ; preds = %305, %301
  %315 = load ptr, ptr %10, align 8
  %316 = getelementptr inbounds nuw %struct.MemPage, ptr %315, i32 0, i32 1
  %317 = load i8, ptr %316, align 1
  %318 = icmp ne i8 %317, 0
  br i1 %318, label %319, label %339

319:                                              ; preds = %314
  %320 = load i32, ptr %21, align 4
  %321 = icmp ne i32 %320, 0
  br i1 %321, label %322, label %327

322:                                              ; preds = %319
  %323 = getelementptr inbounds nuw %struct.CellInfo, ptr %36, i32 0, i32 0
  %324 = load i64, ptr %323, align 8
  %325 = load i64, ptr %9, align 8
  %326 = icmp sgt i64 %324, %325
  br i1 %326, label %332, label %336

327:                                              ; preds = %319
  %328 = getelementptr inbounds nuw %struct.CellInfo, ptr %36, i32 0, i32 0
  %329 = load i64, ptr %328, align 8
  %330 = load i64, ptr %9, align 8
  %331 = icmp sge i64 %329, %330
  br i1 %331, label %332, label %336

332:                                              ; preds = %327, %322
  %333 = load ptr, ptr %6, align 8
  %334 = getelementptr inbounds nuw %struct.CellInfo, ptr %36, i32 0, i32 0
  %335 = load i64, ptr %334, align 8
  call void (ptr, ptr, ...) @checkAppendMsg(ptr noundef %333, ptr noundef @.str.465, i64 noundef %335)
  br label %336

336:                                              ; preds = %332, %327, %322
  %337 = getelementptr inbounds nuw %struct.CellInfo, ptr %36, i32 0, i32 0
  %338 = load i64, ptr %337, align 8
  store i64 %338, ptr %9, align 8
  store i32 0, ptr %21, align 4
  br label %339

339:                                              ; preds = %336, %314
  %340 = getelementptr inbounds nuw %struct.CellInfo, ptr %36, i32 0, i32 2
  %341 = load i32, ptr %340, align 8
  %342 = getelementptr inbounds nuw %struct.CellInfo, ptr %36, i32 0, i32 3
  %343 = load i16, ptr %342, align 4
  %344 = zext i16 %343 to i32
  %345 = icmp ugt i32 %341, %344
  br i1 %345, label %346, label %379

346:                                              ; preds = %339
  %347 = getelementptr inbounds nuw %struct.CellInfo, ptr %36, i32 0, i32 2
  %348 = load i32, ptr %347, align 8
  %349 = getelementptr inbounds nuw %struct.CellInfo, ptr %36, i32 0, i32 3
  %350 = load i16, ptr %349, align 4
  %351 = zext i16 %350 to i32
  %352 = sub i32 %348, %351
  %353 = load i32, ptr %27, align 4
  %354 = add i32 %352, %353
  %355 = sub i32 %354, 5
  %356 = load i32, ptr %27, align 4
  %357 = sub i32 %356, 4
  %358 = udiv i32 %355, %357
  store i32 %358, ptr %37, align 4
  %359 = load ptr, ptr %23, align 8
  %360 = getelementptr inbounds nuw %struct.CellInfo, ptr %36, i32 0, i32 4
  %361 = load i16, ptr %360, align 2
  %362 = zext i16 %361 to i32
  %363 = sub i32 %362, 4
  %364 = sext i32 %363 to i64
  %365 = getelementptr inbounds i8, ptr %359, i64 %364
  %366 = call i32 @sqlite3Get4byte(ptr noundef %365)
  store i32 %366, ptr %38, align 4
  %367 = load ptr, ptr %25, align 8
  %368 = getelementptr inbounds nuw %struct.BtShared, ptr %367, i32 0, i32 5
  %369 = load i8, ptr %368, align 1
  %370 = icmp ne i8 %369, 0
  br i1 %370, label %371, label %375

371:                                              ; preds = %346
  %372 = load ptr, ptr %6, align 8
  %373 = load i32, ptr %38, align 4
  %374 = load i32, ptr %7, align 4
  call void @checkPtrmap(ptr noundef %372, i32 noundef %373, i8 noundef zeroext 3, i32 noundef %374)
  br label %375

375:                                              ; preds = %371, %346
  %376 = load ptr, ptr %6, align 8
  %377 = load i32, ptr %38, align 4
  %378 = load i32, ptr %37, align 4
  call void @checkList(ptr noundef %376, i32 noundef 0, i32 noundef %377, i32 noundef %378)
  br label %379

379:                                              ; preds = %375, %339
  %380 = load ptr, ptr %10, align 8
  %381 = getelementptr inbounds nuw %struct.MemPage, ptr %380, i32 0, i32 4
  %382 = load i8, ptr %381, align 8
  %383 = icmp ne i8 %382, 0
  br i1 %383, label %407, label %384

384:                                              ; preds = %379
  %385 = load ptr, ptr %23, align 8
  %386 = call i32 @sqlite3Get4byte(ptr noundef %385)
  store i32 %386, ptr %15, align 4
  %387 = load ptr, ptr %25, align 8
  %388 = getelementptr inbounds nuw %struct.BtShared, ptr %387, i32 0, i32 5
  %389 = load i8, ptr %388, align 1
  %390 = icmp ne i8 %389, 0
  br i1 %390, label %391, label %395

391:                                              ; preds = %384
  %392 = load ptr, ptr %6, align 8
  %393 = load i32, ptr %15, align 4
  %394 = load i32, ptr %7, align 4
  call void @checkPtrmap(ptr noundef %392, i32 noundef %393, i8 noundef zeroext 5, i32 noundef %394)
  br label %395

395:                                              ; preds = %391, %384
  %396 = load ptr, ptr %6, align 8
  %397 = load i32, ptr %15, align 4
  %398 = load i64, ptr %9, align 8
  %399 = call i32 @checkTreePage(ptr noundef %396, i32 noundef %397, ptr noundef %9, i64 noundef %398)
  store i32 %399, ptr %14, align 4
  store i32 0, ptr %21, align 4
  %400 = load i32, ptr %14, align 4
  %401 = load i32, ptr %13, align 4
  %402 = icmp ne i32 %400, %401
  br i1 %402, label %403, label %406

403:                                              ; preds = %395
  %404 = load ptr, ptr %6, align 8
  call void (ptr, ptr, ...) @checkAppendMsg(ptr noundef %404, ptr noundef @.str.466)
  %405 = load i32, ptr %14, align 4
  store i32 %405, ptr %13, align 4
  br label %406

406:                                              ; preds = %403, %395
  br label %418

407:                                              ; preds = %379
  %408 = load ptr, ptr %29, align 8
  %409 = load i32, ptr %26, align 4
  %410 = shl i32 %409, 16
  %411 = load i32, ptr %26, align 4
  %412 = getelementptr inbounds nuw %struct.CellInfo, ptr %36, i32 0, i32 4
  %413 = load i16, ptr %412, align 2
  %414 = zext i16 %413 to i32
  %415 = add i32 %411, %414
  %416 = sub i32 %415, 1
  %417 = or i32 %410, %416
  call void @btreeHeapInsert(ptr noundef %408, i32 noundef %417)
  br label %418

418:                                              ; preds = %407, %406
  br label %419

419:                                              ; preds = %418, %312, %299, %276
  %420 = load i32, ptr %11, align 4
  %421 = add i32 %420, -1
  store i32 %421, ptr %11, align 4
  br label %242, !llvm.loop !6

422:                                              ; preds = %250
  %423 = load i64, ptr %9, align 8
  %424 = load ptr, ptr %8, align 8
  store i64 %423, ptr %424, align 8
  %425 = load ptr, ptr %6, align 8
  %426 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %425, i32 0, i32 9
  store ptr null, ptr %426, align 8
  %427 = load i32, ptr %20, align 4
  %428 = icmp ne i32 %427, 0
  br i1 %428, label %429, label %624

429:                                              ; preds = %422
  %430 = load ptr, ptr %6, align 8
  %431 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %430, i32 0, i32 4
  %432 = load i32, ptr %431, align 4
  %433 = icmp sgt i32 %432, 0
  br i1 %433, label %434, label %624

434:                                              ; preds = %429
  %435 = load ptr, ptr %10, align 8
  %436 = getelementptr inbounds nuw %struct.MemPage, ptr %435, i32 0, i32 4
  %437 = load i8, ptr %436, align 8
  %438 = icmp ne i8 %437, 0
  br i1 %438, label %495, label %439

439:                                              ; preds = %434
  %440 = load ptr, ptr %6, align 8
  %441 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %440, i32 0, i32 14
  %442 = load ptr, ptr %441, align 8
  store ptr %442, ptr %29, align 8
  %443 = load ptr, ptr %29, align 8
  %444 = getelementptr inbounds i32, ptr %443, i64 0
  store i32 0, ptr %444, align 4
  %445 = load i32, ptr %19, align 4
  %446 = sub i32 %445, 1
  store i32 %446, ptr %11, align 4
  br label %447

447:                                              ; preds = %491, %439
  %448 = load i32, ptr %11, align 4
  %449 = icmp sge i32 %448, 0
  br i1 %449, label %450, label %494

450:                                              ; preds = %447
  %451 = load ptr, ptr %22, align 8
  %452 = load i32, ptr %18, align 4
  %453 = load i32, ptr %11, align 4
  %454 = mul i32 %453, 2
  %455 = add i32 %452, %454
  %456 = sext i32 %455 to i64
  %457 = getelementptr inbounds i8, ptr %451, i64 %456
  %458 = getelementptr inbounds i8, ptr %457, i64 0
  %459 = load i8, ptr %458, align 1
  %460 = zext i8 %459 to i32
  %461 = shl i32 %460, 8
  %462 = load ptr, ptr %22, align 8
  %463 = load i32, ptr %18, align 4
  %464 = load i32, ptr %11, align 4
  %465 = mul i32 %464, 2
  %466 = add i32 %463, %465
  %467 = sext i32 %466 to i64
  %468 = getelementptr inbounds i8, ptr %462, i64 %467
  %469 = getelementptr inbounds i8, ptr %468, i64 1
  %470 = load i8, ptr %469, align 1
  %471 = zext i8 %470 to i32
  %472 = or i32 %461, %471
  store i32 %472, ptr %26, align 4
  %473 = load ptr, ptr %10, align 8
  %474 = getelementptr inbounds nuw %struct.MemPage, ptr %473, i32 0, i32 23
  %475 = load ptr, ptr %474, align 8
  %476 = load ptr, ptr %10, align 8
  %477 = load ptr, ptr %22, align 8
  %478 = load i32, ptr %26, align 4
  %479 = zext i32 %478 to i64
  %480 = getelementptr inbounds nuw i8, ptr %477, i64 %479
  %481 = call zeroext i16 %475(ptr noundef %476, ptr noundef %480)
  %482 = zext i16 %481 to i32
  store i32 %482, ptr %39, align 4
  %483 = load ptr, ptr %29, align 8
  %484 = load i32, ptr %26, align 4
  %485 = shl i32 %484, 16
  %486 = load i32, ptr %26, align 4
  %487 = load i32, ptr %39, align 4
  %488 = add i32 %486, %487
  %489 = sub i32 %488, 1
  %490 = or i32 %485, %489
  call void @btreeHeapInsert(ptr noundef %483, i32 noundef %490)
  br label %491

491:                                              ; preds = %450
  %492 = load i32, ptr %11, align 4
  %493 = add i32 %492, -1
  store i32 %493, ptr %11, align 4
  br label %447, !llvm.loop !8

494:                                              ; preds = %447
  br label %495

495:                                              ; preds = %494, %434
  %496 = load ptr, ptr %22, align 8
  %497 = load i32, ptr %17, align 4
  %498 = add i32 %497, 1
  %499 = sext i32 %498 to i64
  %500 = getelementptr inbounds i8, ptr %496, i64 %499
  %501 = getelementptr inbounds i8, ptr %500, i64 0
  %502 = load i8, ptr %501, align 1
  %503 = zext i8 %502 to i32
  %504 = shl i32 %503, 8
  %505 = load ptr, ptr %22, align 8
  %506 = load i32, ptr %17, align 4
  %507 = add i32 %506, 1
  %508 = sext i32 %507 to i64
  %509 = getelementptr inbounds i8, ptr %505, i64 %508
  %510 = getelementptr inbounds i8, ptr %509, i64 1
  %511 = load i8, ptr %510, align 1
  %512 = zext i8 %511 to i32
  %513 = or i32 %504, %512
  store i32 %513, ptr %11, align 4
  br label %514

514:                                              ; preds = %517, %495
  %515 = load i32, ptr %11, align 4
  %516 = icmp sgt i32 %515, 0
  br i1 %516, label %517, label %561

517:                                              ; preds = %514
  %518 = load ptr, ptr %22, align 8
  %519 = load i32, ptr %11, align 4
  %520 = add i32 %519, 2
  %521 = sext i32 %520 to i64
  %522 = getelementptr inbounds i8, ptr %518, i64 %521
  %523 = getelementptr inbounds i8, ptr %522, i64 0
  %524 = load i8, ptr %523, align 1
  %525 = zext i8 %524 to i32
  %526 = shl i32 %525, 8
  %527 = load ptr, ptr %22, align 8
  %528 = load i32, ptr %11, align 4
  %529 = add i32 %528, 2
  %530 = sext i32 %529 to i64
  %531 = getelementptr inbounds i8, ptr %527, i64 %530
  %532 = getelementptr inbounds i8, ptr %531, i64 1
  %533 = load i8, ptr %532, align 1
  %534 = zext i8 %533 to i32
  %535 = or i32 %526, %534
  store i32 %535, ptr %40, align 4
  %536 = load ptr, ptr %29, align 8
  %537 = load i32, ptr %11, align 4
  %538 = shl i32 %537, 16
  %539 = load i32, ptr %11, align 4
  %540 = load i32, ptr %40, align 4
  %541 = add i32 %539, %540
  %542 = sub i32 %541, 1
  %543 = or i32 %538, %542
  call void @btreeHeapInsert(ptr noundef %536, i32 noundef %543)
  %544 = load ptr, ptr %22, align 8
  %545 = load i32, ptr %11, align 4
  %546 = sext i32 %545 to i64
  %547 = getelementptr inbounds i8, ptr %544, i64 %546
  %548 = getelementptr inbounds i8, ptr %547, i64 0
  %549 = load i8, ptr %548, align 1
  %550 = zext i8 %549 to i32
  %551 = shl i32 %550, 8
  %552 = load ptr, ptr %22, align 8
  %553 = load i32, ptr %11, align 4
  %554 = sext i32 %553 to i64
  %555 = getelementptr inbounds i8, ptr %552, i64 %554
  %556 = getelementptr inbounds i8, ptr %555, i64 1
  %557 = load i8, ptr %556, align 1
  %558 = zext i8 %557 to i32
  %559 = or i32 %551, %558
  store i32 %559, ptr %41, align 4
  %560 = load i32, ptr %41, align 4
  store i32 %560, ptr %11, align 4
  br label %514, !llvm.loop !9

561:                                              ; preds = %514
  store i32 0, ptr %16, align 4
  %562 = load i32, ptr %28, align 4
  %563 = sub i32 %562, 1
  store i32 %563, ptr %31, align 4
  br label %564

564:                                              ; preds = %589, %561
  %565 = load ptr, ptr %29, align 8
  %566 = call i32 @btreeHeapPull(ptr noundef %565, ptr noundef %30)
  %567 = icmp ne i32 %566, 0
  br i1 %567, label %568, label %590

568:                                              ; preds = %564
  %569 = load i32, ptr %31, align 4
  %570 = and i32 %569, 65535
  %571 = load i32, ptr %30, align 4
  %572 = lshr i32 %571, 16
  %573 = icmp uge i32 %570, %572
  br i1 %573, label %574, label %579

574:                                              ; preds = %568
  %575 = load ptr, ptr %6, align 8
  %576 = load i32, ptr %30, align 4
  %577 = lshr i32 %576, 16
  %578 = load i32, ptr %7, align 4
  call void (ptr, ptr, ...) @checkAppendMsg(ptr noundef %575, ptr noundef @.str.467, i32 noundef %577, i32 noundef %578)
  br label %590

579:                                              ; preds = %568
  %580 = load i32, ptr %30, align 4
  %581 = lshr i32 %580, 16
  %582 = load i32, ptr %31, align 4
  %583 = and i32 %582, 65535
  %584 = sub i32 %581, %583
  %585 = sub i32 %584, 1
  %586 = load i32, ptr %16, align 4
  %587 = add i32 %586, %585
  store i32 %587, ptr %16, align 4
  %588 = load i32, ptr %30, align 4
  store i32 %588, ptr %31, align 4
  br label %589

589:                                              ; preds = %579
  br label %564, !llvm.loop !10

590:                                              ; preds = %574, %564
  %591 = load i32, ptr %27, align 4
  %592 = load i32, ptr %31, align 4
  %593 = and i32 %592, 65535
  %594 = sub i32 %591, %593
  %595 = sub i32 %594, 1
  %596 = load i32, ptr %16, align 4
  %597 = add i32 %596, %595
  store i32 %597, ptr %16, align 4
  %598 = load ptr, ptr %29, align 8
  %599 = getelementptr inbounds i32, ptr %598, i64 0
  %600 = load i32, ptr %599, align 4
  %601 = icmp eq i32 %600, 0
  br i1 %601, label %602, label %623

602:                                              ; preds = %590
  %603 = load i32, ptr %16, align 4
  %604 = load ptr, ptr %22, align 8
  %605 = load i32, ptr %17, align 4
  %606 = add i32 %605, 7
  %607 = sext i32 %606 to i64
  %608 = getelementptr inbounds i8, ptr %604, i64 %607
  %609 = load i8, ptr %608, align 1
  %610 = zext i8 %609 to i32
  %611 = icmp ne i32 %603, %610
  br i1 %611, label %612, label %623

612:                                              ; preds = %602
  %613 = load ptr, ptr %6, align 8
  %614 = load i32, ptr %16, align 4
  %615 = load ptr, ptr %22, align 8
  %616 = load i32, ptr %17, align 4
  %617 = add i32 %616, 7
  %618 = sext i32 %617 to i64
  %619 = getelementptr inbounds i8, ptr %615, i64 %618
  %620 = load i8, ptr %619, align 1
  %621 = zext i8 %620 to i32
  %622 = load i32, ptr %7, align 4
  call void (ptr, ptr, ...) @checkAppendMsg(ptr noundef %613, ptr noundef @.str.468, i32 noundef %614, i32 noundef %621, i32 noundef %622)
  br label %623

623:                                              ; preds = %612, %602, %590
  br label %624

624:                                              ; preds = %623, %429, %422
  br label %625

625:                                              ; preds = %624, %120, %113, %102, %95
  %626 = load i32, ptr %20, align 4
  %627 = icmp ne i32 %626, 0
  br i1 %627, label %632, label %628

628:                                              ; preds = %625
  %629 = load i8, ptr %35, align 1
  %630 = load ptr, ptr %10, align 8
  %631 = getelementptr inbounds nuw %struct.MemPage, ptr %630, i32 0, i32 0
  store i8 %629, ptr %631, align 8
  br label %632

632:                                              ; preds = %628, %625
  %633 = load ptr, ptr %10, align 8
  call void @releasePage(ptr noundef %633)
  %634 = load ptr, ptr %32, align 8
  %635 = load ptr, ptr %6, align 8
  %636 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %635, i32 0, i32 9
  store ptr %634, ptr %636, align 8
  %637 = load i32, ptr %33, align 4
  %638 = load ptr, ptr %6, align 8
  %639 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %638, i32 0, i32 11
  store i32 %637, ptr %639, align 4
  %640 = load i32, ptr %34, align 4
  %641 = load ptr, ptr %6, align 8
  %642 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %641, i32 0, i32 12
  store i32 %640, ptr %642, align 8
  %643 = load ptr, ptr %6, align 8
  %644 = getelementptr inbounds nuw %struct.IntegrityCk, ptr %643, i32 0, i32 8
  %645 = load i8, ptr %644, align 4
  %646 = add i8 %645, -1
  store i8 %646, ptr %644, align 4
  %647 = load i32, ptr %13, align 4
  %648 = add i32 %647, 1
  store i32 %648, ptr %5, align 4
  br label %649

649:                                              ; preds = %632, %72, %66, %56
  %650 = load i32, ptr %5, align 4
  ret i32 %650
}

; Function Attrs: noinline nounwind optnone sspstrong uwtable
declare i32 @checkRef(ptr noundef, i32 noundef) #0

; Function Attrs: noinline nounwind optnone sspstrong uwtable
declare void @checkProgress(ptr noundef) #0

; Function Attrs: noinline nounwind optnone sspstrong uwtable
declare void @btreeHeapInsert(ptr noundef, i32 noundef) #0

; Function Attrs: noinline nounwind optnone sspstrong uwtable
declare i32 @btreeHeapPull(ptr noundef, ptr noundef) #0

attributes #0 = { noinline nounwind optnone sspstrong uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="4" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "zero-call-used-regs"="used-gpr" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 4, !"probe-stack", !"inline-asm"}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 22.1.8"}
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}
!8 = distinct !{!8, !7}
!9 = distinct !{!9, !7}
!10 = distinct !{!10, !7}
