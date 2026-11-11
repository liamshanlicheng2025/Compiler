# 静态条目（Static items）

> 中文译文，原文：`reference/RCompiler-Spec/src/items/static-items.md`

r[items.static.syntax]
```grammar,items
StaticItem ->
    ItemSafety? `static` `mut`? IDENTIFIER `:` Type ( `=` Expression )? `;`
```

（注：`safe` 和 `unsafe` 限定符在语义上只允许出现在 `extern` 块中。）

r[items.static.intro]
*静态条目*与常量类似，不同之处在于它表示程序中的一块分配（allocation），并用初始化表达式初始化。所有指向该静态条目的引用和裸指针都指向同一块分配。

r[items.static.lifetime]
静态条目具有 `static` 生命周期，它比 Rust 程序中所有其他生命周期都长。程序结束时静态条目不会调用 `drop`。

r[items.static.storage-disjointness]
如果 `static` 的大小至少为 1 字节，这块分配与所有其他此类 `static` 分配、堆分配以及栈上变量都不重叠。但是，不可变 `static` 条目的存储可以与本身没有唯一地址的分配重叠，例如被常量提升的值（promoteds）和 `const` 条目。

r[items.static.namespace]
静态声明把静态值定义在其所在模块或块的值命名空间中。

r[items.static.init]
静态初始化表达式是编译期求值的常量表达式。静态初始化表达式可以引用并读取其他静态条目。当读取可变静态条目时，读到的是该静态条目的初始值。

r[items.static.read-only]
不含内部可变性（interior mutable）类型的非 `mut` 静态条目，可以被放在只读内存中。

r[items.static.safety]
对静态条目的所有访问都是安全的，但静态条目有若干限制：

r[items.static.sync]
* 类型必须有 `Sync` trait 约束，以允许线程安全的访问。

r[items.static.init.omission]
在外部块（external block）中必须省略初始化表达式；而独立的静态条目必须提供初始化表达式。

r[items.static.safety-qualifiers]
`safe` 和 `unsafe` 限定符在语义上只允许用于外部块中。

r[items.static.generics]
## 静态条目与泛型

在泛型作用域中定义的静态条目（例如在全覆盖实现或默认实现中），最终只会定义出恰好一个静态条目，就好像这个 static 定义被从当前作用域抽出来放进了模块一样。不会每个单态化各有一个条目。

这段代码：

```rust
use std::sync::atomic::{AtomicUsize, Ordering};

trait Tr {
    fn default_impl() {
        static COUNTER: AtomicUsize = AtomicUsize::new(0);
        println!("default_impl: counter was {}", COUNTER.fetch_add(1, Ordering::Relaxed));
    }

    fn blanket_impl();
}

struct Ty1 {}
struct Ty2 {}

impl<T> Tr for T {
    fn blanket_impl() {
        static COUNTER: AtomicUsize = AtomicUsize::new(0);
        println!("blanket_impl: counter was {}", COUNTER.fetch_add(1, Ordering::Relaxed));
    }
}

fn main() {
    <Ty1 as Tr>::default_impl();
    <Ty2 as Tr>::default_impl();
    <Ty1 as Tr>::blanket_impl();
    <Ty2 as Tr>::blanket_impl();
}
```

打印：

```text
default_impl: counter was 0
default_impl: counter was 1
blanket_impl: counter was 0
blanket_impl: counter was 1
```

r[items.static.mut]
## 可变静态条目（Mutable statics）

r[items.static.mut.intro]
如果静态条目声明时带 `mut` 关键字，那么程序可以修改它。Rust 的目标之一是让并发 bug 难以出现，而可变静态显然是非常容易产生竞态条件或其他 bug 的来源。

r[items.static.mut.safety]
因此，无论读取还是写入可变静态变量，都需要 `unsafe` 块。应当小心确保对可变静态的修改相对同一进程中运行的其他线程是安全的。

r[items.static.mut.extern]
不过，可变静态仍然很有用。它们可以与 C 库配合使用，也可以在 `extern` 块中绑定来自 C 库的符号。

```rust
# fn atomic_add(_: *mut u32, _: u32) -> u32 { 2 }

static mut LEVELS: u32 = 0;

// 这违反了"无共享状态"的理念，而且内部也没有
// 对竞态的保护，所以这个函数是 `unsafe` 的
unsafe fn bump_levels_unsafe() -> u32 {
    unsafe {
        let ret = LEVELS;
        LEVELS += 1;
        return ret;
    }
}

// 作为 `bump_levels_unsafe` 的替代，假设我们有一个返回旧值的
// atomic_add 函数，那么这个函数是安全的。它的安全性仅在没有其他
// 代码以非原子方式访问该静态条目时成立。如果这样的访问可能存在
// （如 `bump_levels_unsafe` 中），那么它就需要标记为 `unsafe`，
// 告诉调用者他们仍然必须防范并发访问。
fn bump_levels_safe() -> u32 {
    unsafe {
        return atomic_add(&raw mut LEVELS, 1);
    }
}
```

r[items.static.mut.sync]
可变静态条目与普通静态条目的限制相同，除了类型不必实现 `Sync` trait。

r[items.static.alternate]
## 用 Static 还是 Const

到底该用常量条目还是静态条目，可能会让人困惑。一般来说应该优先用常量，除非满足以下情形之一：

* 要存储大量数据。
* 需要静态条目的"唯一地址"性质。
* 需要内部可变性。
