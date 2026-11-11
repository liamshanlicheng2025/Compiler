target triple = "riscv32-unknown-none-elf"

declare void @printInt(i32)
declare void @printlnInt(i32)
declare i32 @getInt()
declare void @exit(i32)




define i32 @add(i32 %a, i32 %b) {
entry:
  %a.addr = alloca i32
  store i32 %a, ptr %a.addr
  %b.addr = alloca i32
  store i32 %b, ptr %b.addr
  %0 = load i32, ptr %a.addr
  %1 = load i32, ptr %b.addr
  %2 = add i32 %0, %1
  ret i32 %2
}
define i32 @main() {
entry:
  %s.addr = alloca i32
  %0 = call i32 @add(i32 3, i32 4)
  store i32 %0, ptr %s.addr
  %1 = load i32, ptr %s.addr
  call void @printlnInt(i32 %1)
  ret i32 0
}
