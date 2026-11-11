target triple = "riscv32-unknown-none-elf"

declare void @printInt(i32)
declare void @printlnInt(i32)
declare i32 @getInt()
declare void @exit(i32)

%struct.Point = type { i32, i32 }
%struct.Line = type { %struct.Point, %struct.Point }


define i32 @main() {
entry:
  ret i32 0
}
