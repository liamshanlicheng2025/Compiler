target triple = "riscv32-unknown-none-elf"

declare void @printInt(i32)
declare void @printlnInt(i32)
declare i32 @getInt()
declare void @exit(i32)



define i32 @main() {
entry:
  %x.addr = alloca i32
  %0 = add i32 1, 2
  store i32 %0, ptr %x.addr
  %y.addr = alloca i32
  %1 = load i32, ptr %x.addr
  %2 = mul i32 %1, 3
  store i32 %2, ptr %y.addr
  %3 = load i32, ptr %y.addr
  %4 = add i32 %3, 4
  store i32 %4, ptr %y.addr
  %5 = load i32, ptr %y.addr
  call void @printlnInt(i32 %5)
  ret i32 0
}
