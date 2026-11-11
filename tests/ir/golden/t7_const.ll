target triple = "riscv32-unknown-none-elf"

declare void @printInt(i32)
declare void @printlnInt(i32)
declare i32 @getInt()
declare void @exit(i32)



define i32 @main() {
entry:
  %x.addr = alloca i32
  store i32 42, ptr %x.addr
  %0 = load i32, ptr %x.addr
  call void @printlnInt(i32 %0)
  ret i32 0
}
