# 单元类型（Unit type）

> 中文译文，原文：`reference/RCompiler-Spec/src/types/unit.md`

r[type.tuple.syntax]
```grammar,types
UnitType -> `(` `)`
```

r[type.tuple.unit]
单元类型其实就是没有字段的元组类型（`()`）。它唯一的值也叫 *unit* 或*单元值*。

r[type.unit.usage]
单元类型常见于：

- 没有显式返回类型的函数默认返回 `()`。
- 块表达式 `{ ... }` 返回其最后一个表达式的值；如果没有，则返回 `()`。
- 有些表达式总是求值为 `()`，它们是：
  - 赋值表达式和复合赋值表达式（见运算符表达式）。
  - 不带值 break 的循环表达式。`while` 循环总是求值为 `()`。

r[type.unit.example]
一些例子：

```rust
fn main() -> () {
  let mut x = 5;    
  let y: () = x = 8;
  let z: ();
  z = while (x < 10) {
      x += 1;
      printlnInt(x); // 9\n10\n
  };
  let mut w = 2;
  let a: [(); 5] = [w += 1; 5];
  printlnInt(w); // 3
  let b: [(); 3] = [w += 1, w += 1, w += 1];
  printlnInt(w); // 6
}
```

r[type.unit.note]
单元类型不需要显式写出构造器 `()`。例如：

```rust
let x: () = (); // 这个 () 不是必需的
```
