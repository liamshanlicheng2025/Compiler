# 布尔类型（Boolean type）

> 中文译文，原文：`reference/RCompiler-Spec/src/types/boolean.md`

```rust
let b: bool = true;
```

r[type.bool.intro]
*布尔类型*（*bool*）是一种原生数据类型，只能取两个值之一：*true* 和 *false*。

r[type.bool.literal]
该类型的值可以用字面量表达式创建，关键字 `true` 和 `false` 分别对应同名的值。

r[type.bool.namespace]
该类型属于语言预置（language prelude），名字为 `bool`。

r[type.bool.layout]
布尔类型的对象，大小和对齐都是 1 字节。

r[type.bool.repr]
值 false 的位模式是 `0x00`，值 true 的位模式是 `0x01`。布尔类型的对象出现任何其他位模式都属于未定义行为。

r[type.bool.usage]
布尔类型是多种表达式中操作数的类型：

r[type.bool.usage-condition]
* if 表达式和 while 表达式中的条件操作数

r[type.bool.usage-lazy-operator]
* 惰性布尔运算符表达式（`&&`、`||`）中的操作数

> [!NOTE]
> 布尔类型表现得类似枚举类型，但它并不是枚举类型。实践中这主要意味着该类型没有关联的构造器（例如不存在 `bool::true` 这种写法）。

r[type.bool.traits]
与所有原生类型一样，布尔类型实现了 trait `Clone`、`Copy`、`Sized`、`Send` 和 `Sync`。

> [!NOTE]
> 库层面的操作请见标准库文档。

r[type.bool.expr]
## 布尔值的运算

当某些运算符表达式的操作数是布尔类型时，按布尔逻辑的规则求值。

r[type.bool.expr.not]
### 逻辑非

| `b` | `!b` |
|- | - |
| `true` | `false` |
| `false` | `true` |

r[type.bool.expr.or]
### 逻辑或

| `a` | `b` | <code>a &#124; b</code> |
|- | - | - |
| `true` | `true` | `true` |
| `true` | `false` | `true` |
| `false` | `true` | `true` |
| `false` | `false` | `false` |

r[type.bool.expr.and]
### 逻辑与

| `a` | `b` | `a & b` |
|- | - | - |
| `true` | `true` | `true` |
| `true` | `false` | `false` |
| `false` | `true` | `false` |
| `false` | `false` | `false` |

r[type.bool.expr.xor]
### 逻辑异或

| `a` | `b` | `a ^ b` |
|- | - | - |
| `true` | `true` | `false` |
| `true` | `false` | `true` |
| `false` | `true` | `true` |
| `false` | `false` | `false` |

r[type.bool.expr.cmp]
### 比较

r[type.bool.expr.cmp.eq]
| `a` | `b` | `a == b` |
|- | - | - |
| `true` | `true` | `true` |
| `true` | `false` | `false` |
| `false` | `true` | `false` |
| `false` | `false` | `true` |

r[type.bool.expr.cmp.greater]
| `a` | `b` | `a > b` |
|- | - | - |
| `true` | `true` | `false` |
| `true` | `false` | `true` |
| `false` | `true` | `false` |
| `false` | `false` | `false` |

r[type.bool.expr.cmp.not-eq]
* `a != b` 等价于 `!(a == b)`

r[type.bool.expr.cmp.greater-eq]
* `a >= b` 等价于 `a == b | a > b`

r[type.bool.expr.cmp.less]
* `a < b` 等价于 `!(a >= b)`

r[type.bool.expr.cmp.less-eq]
* `a <= b` 等价于 `a == b | a < b`

r[type.bool.validity]
## 位有效性（Bit validity）

`bool` 的那一个字节保证是已初始化的（换句话说，`transmute::<bool, u8>(...)` 永远是安全的——但由于某些位模式不是合法的 `bool`，反方向的转换并不总是安全的）。
