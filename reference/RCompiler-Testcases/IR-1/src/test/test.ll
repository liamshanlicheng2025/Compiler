target triple = "riscv32-unknown-unknown-elf"

declare dso_local void @print(ptr)
declare dso_local void @println(ptr)
declare dso_local void @printInt(i32)
declare dso_local void @printlnInt(i32)
declare dso_local ptr @getString()
declare dso_local i32 @getInt()

declare dso_local ptr @builtin_memset(ptr nocapture writeonly, i8, i32)
declare dso_local ptr @builtin_memcpy(ptr nocapture writeonly, ptr nocapture readonly, i32)

define i32 @main() {
start:
  %i = alloca [4 x i8], align 4
  %stack = alloca [68 x i8], align 4
  call void @new(ptr sret([68 x i8]) align 4 %stack)
  store i32 0, ptr %i, align 4
  br label %bb2

bb2:                                              ; preds = %start
  %_4 = load i32, ptr %i, align 4
  %_3 = icmp slt i32 %_4, 10
  br i1 %_3, label %bb3, label %bb6

bb3:                                              ; preds = %bb2
  %_7 = call i32 @getInt() align 4
  call void @push(ptr align 4 %stack, i32 signext %_7)
  %0 = load i32, ptr %i, align 4
  %1 = add i32 %0, 1
  store i32 %1, ptr %i, align 4
  br label %bb2

bb6:                                              ; preds = %bb2, %bb9
  %_9 = call zeroext i1 @empty(ptr align 4 %stack)
  br i1 %_9, label %bb8, label %bb9

bb9:                                              ; preds = %bb6
  %_11 = call i32 @pop(ptr align 4 %stack)
  call void @printlnInt(i32 signext %_11)
  br label %bb6

bb8:                                              ; preds = %bb6
  ret i32 0

}

define void @new(ptr sret([68 x i8]) align 4 %_0) {
start:
  %_1 = alloca [64 x i8], align 4
  %ms = call ptr @builtin_memset(ptr align 4 %_1, i8 0, i32 64, i1 false)
  %mc = call ptr @builtin_memcpy(ptr align 4 %_0, ptr align 4 %_1, i32 64, i1 false)
  %0 = getelementptr i8, ptr %_0, i32 64
  store i32 0, ptr %0, align 4
  ret void
}

define void @push(ptr align 4 %self, i32 signext %value) {
start:
  %0 = getelementptr i8, ptr %self, i32 64
  %_3 = load i32, ptr %0, align 4
  %1 = getelementptr i32, ptr %self, i32 %_3
  store i32 %value, ptr %1, align 4
  %2 = getelementptr i8, ptr %self, i32 64
  %3 = load i32, ptr %2, align 4
  %_5 = add i32 %3, 1
  %4 = getelementptr i8, ptr %self, i32 64
  store i32 %_5, ptr %4, align 4
  ret void

}

define i32 @pop(ptr align 4 %self) {
start:
  %_0 = alloca [4 x i8], align 4
  %0 = getelementptr i8, ptr %self, i32 64
  %_2 = load i32, ptr %0, align 4
  %1 = icmp eq i32 %_2, 0
  br i1 %1, label %bb1, label %bb2

bb1:                                              ; preds = %start
  store i32 0, ptr %_0, align 4
  br label %bb5

bb2:                                              ; preds = %start
  %2 = getelementptr i8, ptr %self, i32 64
  %3 = load i32, ptr %2, align 4
  %_3.0 = sub i32 %3, 1
  %4 = getelementptr i8, ptr %self, i32 64
  store i32 %_3.0, ptr %4, align 4
  %5 = getelementptr i8, ptr %self, i32 64
  %_4 = load i32, ptr %5, align 4
  %6 = getelementptr i32, ptr %self, i32 %_4
  %7 = load i32, ptr %6, align 4
  store i32 %7, ptr %_0, align 4
  br label %bb5

bb5:                                              ; preds = %bb2, %bb1
  %8 = load i32, ptr %_0, align 4
  ret i32 %8
}

define zeroext i1 @empty(ptr align 4 %self) {
start:
  %0 = getelementptr i8, ptr %self, i32 64
  %_2 = load i32, ptr %0, align 4
  %_0 = icmp eq i32 %_2, 0
  ret i1 %_0
}
