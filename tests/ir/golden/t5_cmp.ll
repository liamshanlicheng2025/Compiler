target triple = "riscv32-unknown-none-elf"

declare void @printInt(i32)
declare void @printlnInt(i32)
declare i32 @getInt()
declare void @exit(i32)



define i32 @main() {
entry:
  %a.addr = alloca i32
  store i32 3, ptr %a.addr
  %b.addr = alloca i32
  store i32 4, ptr %b.addr
  %c.addr = alloca i8
  %0 = load i32, ptr %a.addr
  %1 = load i32, ptr %b.addr
  %2 = icmp slt i32 %0, %1
  %3 = zext i1 %2 to i8
  store i8 %3, ptr %c.addr
  %d.addr = alloca i8
  %4 = load i32, ptr %a.addr
  %5 = load i32, ptr %b.addr
  %6 = icmp eq i32 %4, %5
  %7 = zext i1 %6 to i8
  store i8 %7, ptr %d.addr
  %8 = load i8, ptr %c.addr
  %9 = icmp ne i8 %8, 0
  br i1 %9, label %if.then, label %if.end
if.then:
  call void @printlnInt(i32 1)
  br label %if.end
if.end:
  ret i32 0
}
