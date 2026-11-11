# 常量求值（Constant evaluation）

> 中文译文，原文：`reference/RCompiler-Spec/src/const_eval.md`

r[const-eval.general]
常量求值是在编译期间计算表达式结果的过程。只有表达式的一个子集可以在编译期求值。

r[const-eval.const-expr]
## 常量表达式（Constant expressions）

r[const-eval.const-expr.general]
某些特定形式的表达式——称为常量表达式——可以在编译期求值。

r[const-eval.const-expr.const-context]
在常量上下文（const contexts）中，只允许出现这些表达式，且它们总是在编译期求值。

r[const-eval.const-expr.error]
数组下标越界或溢出等行为，在常量上下文中是编译错误。

r[const-eval.const-expr.list]
以下表达式是常量表达式，只要它们的操作数也都是常量表达式：

r[const-eval.const-expr.literal]
* 字面量，包括类型为 `&str` 的字面量。

r[const-eval.const-expr.path-item]
* 指向常量的路径。不允许递归定义常量。

r[const-eval.const-expr.array]
* 数组表达式。

r[const-eval.const-expr.index]
* 下标表达式，即以 `usize` 做数组下标。

r[const-eval.const-expr.builtin-arith-logic]
* 作用于整数类型、`bool` 和 `char` 的内建取负或算术运算符。

r[const-eval.const-expr.group]
* 括号表达式（Grouped expressions）。

以下表达式*永远不能*用在常量表达式中：

r[const-eval.const-expr.block]
* 块表达式
    * let 语句，以及由此涉及的不可反驳模式，包括可变绑定
    * 赋值表达式
    * 复合赋值表达式
    * 表达式语句

r[const-eval.const-expr.func]
* 常量函数（const functions）。

r[const-eval.const-expr.field]
* 字段表达式。

r[const-eval.const-expr.constructor]
* 结构体表达式。

r[const-eval.const-expr.if]
* if 表达式。

r[const-eval.const-expr.loop]
* loop 和 while 表达式。

r[const-eval.const-expr.borrows]
* 所有形式的借用，除了 `&str`。

r[const-eval.const-expr.deref]
* 解引用表达式。

r[const-eval.const-expr.cast]
* 类型转换（cast）表达式。

r[const-eval.const-expr.builtin-logic]
* 作用于整数类型、`bool` 和 `char` 的内建逻辑、惰性布尔或比较运算符。

r[const-eval.const-context]
## 常量上下文（Const context）

r[const-eval.const-context.general]
*常量上下文*是以下之一：

r[const-eval.const-context.array-length]
* 数组类型的长度表达式

r[const-eval.const-context.repeat-length]
* 数组重复形式的长度表达式

r[const-eval.const-context.init]
* 常量的初始化表达式

在常量上下文中，表达式必须是常量表达式，并在编译期求值。

r[const-eval.const-evaluation]
## 常量求值（Const evaluation）

我们保证：对其他常量条目的所有使用，都在它们被定义之后。因此不需要构建常量条目的依赖图来按正确顺序求值——扫描一遍 AST 就足以求值所有常量条目。
