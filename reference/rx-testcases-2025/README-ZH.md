# Rx 编译器测试用例库

[English](README-EN.md) | [简体中文](README-ZH.md)

欢迎来到 Rx Compiler！本仓库包含了 Rx 编译器课程的测试用例。仓库中的文件按编译器阶段或所评测的工作负载类型进行组织。测试运行器（test runner）通过各个目录下的 `manifest.json` 文件自动发现并加载测试用例。

### 测试分类

- **`lexer/`**：包含分词（词法分析）测试。`accept/` 存放应当成功分词的输入，`reject/` 存放格式错误、应当被拒绝的输入。
- **`parser/`**：包含语法分析测试，同样划分为 `accept/` 和 `reject/` 两类。
- **`semantic/`**：包含针对特定语言特性的程序，通过语义分析进行检查。清单中可以同时包含预期通过和预期被拒绝的程序，通过 `compilation_success` 字段进行区分。
- **`codegen/`**：包含必须能够成功编译并运行的程序。每个测试用例提供单次运行所需的标准输入（stdin）以及预期的标准输出（stdout）。
- **`optimization/`**：包含规模更大或对优化敏感的运行时评测负载。

## 测试目录中的文件

- **`.rx`** —— Rx 源程序文件。
- **`.in`** —— 单次运行时执行的标准输入（stdin）。若某个测试用例需要空输入而非文件，清单中可使用 `null`。
- **`.out`** —— 对应执行所期望的准确标准输出（stdout）。若程序预期不打印任何内容，请使用空文件。
- **`manifest.json`** —— 描述该目录下所有测试用例的非空 JSON 数组。清单中的路径均相对于清单文件所在的目录。

通常的命名习惯是将运行时的输入输出测试文件（I/O 文件）与源文件放在同级目录，例如：

```text
codegen/integer-arithmetic/
├── manifest.json
├── acc-...-i32.rx
├── acc-...-i32.mixed.in
└── acc-...-i32.mixed.out
```

同一个源文件可以对应多组 `io` 对，这在小规模测试、边界情况、大规模数据或其他不同输入场景中非常实用。

## Manifest 格式

`manifest.schema.json` 是 `manifest.json` 的 Schema 定义。每个条目包含：

```json
{
  "source": "program.rx",
  "stage": "semantic",
  "compilation_success": true,
  "description": "Optional explanation",
  "metadata": {}
}
```

各字段说明如下：

- **`source`** —— 该条目对应的源文件。
- **`stage`** —— 测试阶段，取值为 `lex`、`parse`、`semantic`、`codegen` 或 `optimization` 之一。
- **`compilation_success`** —— 该源程序是否预期通过该阶段。正常的编译拒绝（rejection）是合法的负例测试结果；而崩溃（crash）、异常信号（signal）或超时（timeout）则判定为失败。
- **`io`** —— 一个或多个 `{ "input", "output" }` 输入/输出对。对于 `codegen` 和 `optimization` 条目该项为必填，程序在运行前必须先成功编译。
- **`description`** —— 可选的用例说明或人类可读的目的阐述，尤其适用于负例测试用例。
- **`metadata`** —— 可选的编写者或来源信息（provenance），可用于记录哈希值、上游来源、token 以及类似详细信息。
