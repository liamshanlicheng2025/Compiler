target triple = "riscv32-unknown-none-elf"

declare void @printInt(i32)
declare void @printlnInt(i32)
declare i32 @getInt()
declare void @exit(i32)



define i32 @main() {
entry:
  %x.addr = alloca i32
  store i32 1, ptr %x.addr
  %0 = load i32, ptr %x.addr
  %1 = icmp sgt i32 %0, 0
  %2 = zext i1 %1 to i8
  %3 = icmp ne i8 %2, 0
  br i1 %3, label %if.then, label %if.else
if.then:
  store i32 10, ptr %x.addr
  br label %if.end
if.else:
  store i32 20, ptr %x.addr
  br label %if.end
if.end:
  %y.addr = alloca i32
  %4 = load i32, ptr %x.addr
  %5 = icmp eq i32 %4, 10
  %6 = zext i1 %5 to i8
  %7 = icmp ne i8 %6, 0
  %8 = alloca i32
  br i1 %7, label %if.then.1, label %if.else.1
if.then.1:
  store i32 100, ptr %8
  br label %if.end.1
if.else.1:
  store i32 200, ptr %8
  br label %if.end.1
if.end.1:
  %9 = load i32, ptr %8
  store i32 %9, ptr %y.addr
  %10 = load i32, ptr %y.addr
  call void @printlnInt(i32 %10)
  ret i32 0
}
