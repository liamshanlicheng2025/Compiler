target triple = "riscv32-unknown-none-elf"

declare void @printInt(i32)
declare void @printlnInt(i32)
declare i32 @getInt()
declare void @exit(i32)



define i32 @main() {
entry:
  %b.addr = alloca i8
  store i8 1, ptr %b.addr
  %c.addr = alloca i8
  store i8 65, ptr %c.addr
  ret i32 0
}
