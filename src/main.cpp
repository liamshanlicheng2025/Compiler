#include <fstream>
#include <iostream>

#include "antlr4-runtime.h"
#include "RustLexer.h"
#include "RustParser.h"

#include "builder/ast_builder.h"
#include "ast/ast_printer.h"
#include "semantic/crate_index.h"
#include "semantic/const_evaluator.h"
#include "semantic/layout_checker.h"
#include "semantic/body_checker.h"
#include "semantic/semantic_error.h"

int main(int argc, char** argv) {
    // 用法: ./compiler <file.rs> [--tree] [--types]
    //   --tree   只打印 ANTLR 解析树（扩展 AST 时的调试工具）
    //   --types  语义检查后打印表达式类型旁表的条目数（调试用）
    bool showParseTree = false;
    bool showExprTypes = false;
    std::string inputFile;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tree") showParseTree = true;
        else if (arg == "--types") showExprTypes = true;
        else inputFile = arg;
    }
    if (inputFile.empty()) {
        std::cerr << "usage: " << argv[0] << " <file.rs> [--tree]" << std::endl;
        return 1;
    }
    std::ifstream stream(inputFile);
    if (!stream) {
        std::cerr << "cannot open file: " << argv[1] << std::endl;
        return 1;
    }

    // ---- 阶段 1：ANTLR 解析（已就绪，不用动）----
    antlr4::ANTLRInputStream input(stream);
    RustLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    RustParser parser(&tokens);
    RustParser::CrateContext* tree = parser.crate();

    // 语法错误直接判编译失败（semantic 测试集按退出码判定，
    // 带错误继续构建 AST 会把本该失败的用例放行）
    if (parser.getNumberOfSyntaxErrors() > 0) return 1;

    if (showParseTree) {
        std::cout << tree->toStringTree(&parser) << std::endl;
        return 0;
    }

    // ---- 阶段 2：构建 AST ----
    AstBuilder builder;
    CratePtr ast = builder.build(tree);

    // ---- 阶段 3：语义分析 ----
    // CrateIndex（声明收集 + 签名 + 入口检查）→ ConstEvaluator（常量预求值
    // + 依赖环）→ LayoutChecker（struct 按值包含环）→ BodyChecker（函数体检查）
    SymbolTable symtab;
    try {
        CrateIndex collector(symtab);
        collector.run(ast);          // 注册所有顶层名字 + 内建函数 + 入口检查
        ConstEvaluator constEval(symtab);
        constEval.evaluateAll(ast);  // 常量统一预求值 + 依赖环检查
        LayoutChecker layout;
        layout.run(ast);             // struct 按值包含查环（只看形状，不算大小）
        BodyChecker checker(symtab);
        checker.run(ast);            // 名字解析 + 类型检查 + 控制流
        if (showExprTypes)           // 旁表填充情况（IR 阶段的输入）
            std::cout << "[types] exprTypes: " << checker.exprTypes().size()
                      << " entries" << std::endl;
    } catch (const SemanticError& e) {
        std::cerr << "semantic error: " << e.what() << std::endl;
        return 1;
    }

    // 调试用：打印你的 AST，和预期结构对比
    if (ast) {
        ASTPrinter printer(std::cout);
        ast->accept(printer);
    }

    // ---- 之后的阶段（先留位）----
    // 阶段 4：IR 生成
    // 阶段 5：RISC-V 代码生成
    // 阶段 6：优化

    return 0;
}
