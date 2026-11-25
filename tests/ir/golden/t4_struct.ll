target triple = "riscv32-unknown-none-elf"

declare void @printInt(i32)
declare void @printlnInt(i32)
declare i32 @getInt()
declare void @exit(i32)
declare ptr @rx_alloc(i32)
declare void @llvm.memcpy.p0.p0.i32(ptr, ptr, i32, i1)
%struct.Vec = type { ptr, i32, i32 }

%struct.Line = type { %struct.Point, %struct.Point }
%struct.Point = type { i32, i32 }


define i32 @main() {
entry:
  ret i32 0
}
