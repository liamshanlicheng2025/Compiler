; M0 验收用：手写 hello world（对应源程序 fn main() { printlnInt(42); }）
declare void @printlnInt(i32)

define i32 @main() {
entry:
  call void @printlnInt(i32 42)
  ret i32 0
}
