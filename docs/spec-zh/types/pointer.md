# 指针类型（Pointer types）

> 中文译文，原文：`reference/RCompiler-Spec/src/types/pointer.md`

r[type.pointer.intro]
所有指针都是显式的一等值。它们可以被移动或复制、存进数据结构中、从函数返回。

r[type.pointer.reference]
## 引用（`&` 和 `&mut`）

r[type.pointer.reference.syntax]
```grammar,types
ReferenceType -> `&` `mut`? TypeNoBounds
```

r[type.pointer.reference.shared]
### 共享引用（`&`）

r[type.pointer.reference.shared.intro]
共享引用指向由其他值拥有的内存。

r[type.pointer.reference.shared.constraint-mutation]
当创建了一个指向某值的共享引用时，它会阻止对该值的直接修改。内部可变性（interior mutability）在特定情况下是例外。顾名思义，指向同一值的共享引用可以同时存在任意多个。共享引用类型写作 `&type`。

r[type.pointer.reference.shared.copy]
复制引用是"浅"操作：只复制指针本身，也就是说指针是 `Copy` 的。释放一个引用对它指向的值没有影响，但对临时值的引用会让该临时值在引用自身的作用域内保持存活。

r[type.pointer.reference.mut]
### 可变引用（`&mut`）

r[type.pointer.reference.mut.intro]
可变引用指向由其他值拥有的内存。可变引用类型写作 `&mut type`。

r[type.pointer.reference.mut.copy]
一个可变引用（在未被再次借用时）是访问它所指向值的唯一途径，因此它不是 `Copy` 的。

r[type.pointer.validity]
## 位有效性

r[type.pointer.validity.pointer-fragment]
尽管在大多数平台生成的机器码中，指针和引用与 `usize` 很相似，但把引用或指针类型 transmute 成非指针类型的语义目前尚未确定。因此，把指针或引用类型 `P` transmute 成 `[u8; size_of::<P>()]` 可能并不合法。
