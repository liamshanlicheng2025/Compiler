# 作用域（Scopes）

> 中文译文，原文：`reference/RCompiler-Spec/src/names/scopes.md`

r[names.scopes.intro]
*作用域*是源文本中的一块区域，在该区域内可以用一个名字引用某个具名实体。以下各节详细说明作用域的规则和行为——它们取决于实体的种类以及声明的位置。名字如何解析到实体的过程见名字解析一章。关于运行析构器所用的"drop 作用域"的更多信息见析构器（destructors）一章。

r[names.scopes.items]
## 条目作用域（Item scopes）

r[names.scopes.items.module]
直接声明在模块中的条目，其名字的作用域**从模块的开头延伸到模块的结尾**。这些条目也是模块的成员，可以用从模块出发的路径来引用。

r[names.scopes.items.statement]
作为语句声明的条目，其名字的作用域**从条目语句所在块的开头延伸到该块的结尾**。

r[names.scopes.items.duplicate]
在同一个模块或块内，引入与同一命名空间中另一条目同名的条目是错误。

r[names.scopes.items.shadow-prelude]
模块中的条目可以遮蔽预置（prelude）中的条目。

r[names.scopes.associated-items]
### 关联条目作用域（Associated item scopes）

r[names.scopes.associated-items.scope]
关联条目没有作用域概念，只能用从它们所关联的类型或 trait 出发的路径来引用。方法也可以通过调用表达式来引用。

r[names.scopes.associated-items.duplicate]
与模块或块内的条目类似，在 trait 或 impl 内引入与同一命名空间中另一条目同名的条目是错误。

r[names.scopes.pattern-bindings]
## 模式绑定作用域（Pattern binding scopes）

局部变量模式绑定的作用域取决于它在哪里使用：

r[names.scopes.pattern-bindings.let]
* `let` 语句的绑定，范围从 `let` 语句之后直到声明它的块的结尾。

r[names.scopes.pattern-bindings.parameter]
* 函数参数的绑定，范围是函数体内部。

r[names.scopes.pattern-bindings.let-chains]
* `if let` 和 `while let` 的绑定，在后续的条件以及 consequent 块中有效。

r[names.scopes.pattern-bindings.match-arm]
* `match` 分支的绑定，范围是分支守卫（match guard）和分支表达式。

r[names.scopes.pattern-bindings.items]
局部变量的作用域不会延伸到条目声明内部。

### 模式绑定的遮蔽（Pattern binding shadowing）

r[names.scopes.pattern-bindings.shadow]
模式绑定可以遮蔽作用域内的任何名字，但以下情况是错误：

* 常量条目（const items）
* 结构体和枚举的构造器

下面的例子展示局部绑定如何遮蔽条目声明：

```rust
fn shadow_example() {
    // 由于此时作用域内还没有局部变量，这里解析到函数。
    foo(); // 打印 `function`
    let foo = || println!("closure");
    fn foo() { println!("function"); }
    // 这里解析到局部闭包，因为它遮蔽了条目。
    foo(); // 打印 `closure`
}
```

r[names.scopes.prelude]
## 预置作用域（Prelude scopes）

r[names.scopes.prelude.intro]
预置（prelude）把实体引入每个模块的作用域。这些实体不是模块的成员，但在名字解析时会被隐式查询。

r[names.scopes.prelude.layers]
预置是分层的：如果它们包含同名实体，一层会遮蔽另一层。预置之间的遮蔽顺序如下（靠前的可以遮蔽靠后的）：

1. 标准库预置（Standard library prelude）
2. 语言预置（Language prelude）

r[names.scopes.self]
## `Self` 作用域

r[names.scopes.self.intro]
虽然 `Self` 是一个有特殊含义的关键字，但它与名字解析的交互方式类似于普通名字。

r[names.scopes.self.def-scope]
结构体、枚举、联合体、trait 或 impl 定义中隐式的 `Self` 类型，被当作类似泛型参数对待，其作用域方式与泛型类型参数相同。

r[names.scopes.self.impl-scope]
impl 的值命名空间中隐式的 `Self` 构造器，其作用域是 impl 的体内（即 impl 的关联条目）。

```rust
// 结构体定义中的 Self 类型。
struct Recursive {
    f1: Option<Box<Self>>
}

// impl 中的 Self 值构造器。
struct ImplExample();
impl ImplExample {
    fn example() -> Self { // Self 类型
        Self() // Self 值构造器
    }
}
```
