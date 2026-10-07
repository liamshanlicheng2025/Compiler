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
  %y = call i32 @getInt()
  %z = call i32 @getInt()
  %_9 = mul i32 %y, %z
  %_7 = srem i32 %_9, 2147483647
  call void @printlnInt(i32 signext %_7)
  %_15 = add i32 %y, %z
  %_13 = srem i32 %_15, 2147483647
  call void @printlnInt(i32 signext %_13)
  %_21 = mul i32 %y, %z
  %_19 = sdiv i32 %_21, %z
  call void @printlnInt(i32 signext %_19)
  ret i32 0
}