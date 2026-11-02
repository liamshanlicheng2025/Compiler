# CI/CD 记录（semantic 阶段至今）

> 整理日期：2026-11-03
> 仓库：https://github.com/liamshanlicheng2025/Compiler

## 1. 本项目的 CI/CD 含义

- **CI（持续集成）**：每次提交后重新构建编译器并跑全量回归测试，
  保证仓库在任何时间点都处于"可构建、测试达标"的状态。
- **记录形式**：GitHub Actions 工作流（`.github/workflows/ci.yml`，
  2026-11-03 启用）对每次 push 自动执行 构建 → 语义回归 → 产出报告，
  运行历史见仓库 Actions 页。启用前的阶段以本地回归记录为准（见 §3）。

## 2. CI 流水线内容

| 步骤 | 内容 | 通过标准 |
|---|---|---|
| 环境准备 | ANTLR 4.13.2（tool jar + C++ runtime，缓存复用）、Java 17、uuid-dev | 安装成功 |
| 构建 | cmake 全量构建（含 ANTLR 代码生成） | 编译链接无错误 |
| 语义回归（旧套件） | `scripts/regress_semantic.py` | semantic-1 221/221、semantic-2 50/50 |
| 语义回归（新套件） | `scripts/regress_rx.py` | TOTAL ≥ 154/239（基线） |
| 报告归档 | 两次回归的完整输出上传为 artifact | —— |

说明：新套件未满分的原因是其中 vec/trait/lifetime 等用例超出课程要求的
语言子集；已知缺口清单见各子目录计数。基线随开发进度上调。

## 3. 提交时间线（semantic 阶段起）

| commit | 日期 | 内容 | 验证 |
|---|---|---|---|
| `04cdbef` | 10-19 | 语义分析：两遍扫描 + 符号表 + 结构化类型 + shared_ptr AST | semantic-1/2 全过 |
| `e8d2777` | 10-21 | 按阶段模型重组语义模块；同步 2025 官方测试集 | 回归全过 |
| `03e2886` | 10-22 | 入口检查、struct 布局查环、常量预求值 | 回归全过 |
| `2693e09` | 10-28 | impl 关联常量支持 | 回归全过 |
| `af52fa1` | 10-28 | 表达式类型旁表 exprTypes（语义→IR 的桥） | 回归全过 |
| `9eeb34e` | 10-28 | IR 阶段计划文档 + LLVM IR 语法入门文档 | 文档 |
| `21884ea` | 10-28 | M0：IR 工具链端到端验证（clang-15 + ravel） | 手写 hello.ll 跑通，输出 42 |
| 本次 | 11-03 | 回归脚本入库、GitHub Actions 启用；IR 生成器 M1 骨架 + M2 起步 | CI 首次运行 |

AST 阶段（09-28 ~ 10-19）的提交记录见 `git log`，包含 ANTLR 工具链搭建、
AST 五波骨架建设、cr 后的类型系统重构等。

## 4. 测试资产

| 资产 | 位置 | 说明 |
|---|---|---|
| 旧官方套件 | `reference/RCompiler-Testcases/semantic-{1,2}` | 221 + 50 个编译期判定用例 |
| 新官方套件 | `reference/rx-compiler-testcases/semantic` | 239 个用例，18 个子目录 |
| IR 套件 | `reference/RCompiler-Testcases/IR-1` | 运行期判定（输出 + 退出码） |
| 本地流水线 | `scripts/run_ir.bash` | .ll → clang → riscv32 汇编 → ravel 模拟器 |
| 回归脚本 | `scripts/regress_semantic.py`、`scripts/regress_rx.py` | 一键全量回归 |

## 5. 如何查看 CI 记录

仓库主页 → Actions 标签 → 每次 push 对应一条运行记录，
绿勾 = 构建+回归全部达标；点进单条记录可下载 regression-reports
artifact 查看各套件逐目录计数。
