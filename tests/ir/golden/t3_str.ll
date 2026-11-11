target triple = "riscv32-unknown-none-elf"

declare void @printInt(i32)
declare void @printlnInt(i32)
declare i32 @getInt()
declare void @exit(i32)



@.str.0 = private unnamed_addr constant [6 x i8] c"hello\00"
@.str.1 = private unnamed_addr constant [6 x i8] c"hello\00"
@.str.2 = private unnamed_addr constant [6 x i8] c"world\00"

define i32 @main() {
entry:
  %s.addr = alloca ptr
  %0 = getelementptr inbounds [6 x i8], ptr @.str.0, i32 0, i32 0
  store ptr %0, ptr %s.addr
  %t.addr = alloca ptr
  %1 = getelementptr inbounds [6 x i8], ptr @.str.1, i32 0, i32 0
  store ptr %1, ptr %t.addr
  %u.addr = alloca ptr
  %2 = getelementptr inbounds [6 x i8], ptr @.str.2, i32 0, i32 0
  store ptr %2, ptr %u.addr
  ret i32 0
}
