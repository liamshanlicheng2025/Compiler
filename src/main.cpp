#include <fstream>
#include <iostream>

#include "antlr4-runtime.h"
#include "RustLexer.h"
#include "RustParser.h"

#include "builder/ast_builder.h"
#include "ast/ast_printer.h"

int main(int argc, char** argv) {
    // 用法: ./compiler <file.rs> [--tree]
    //   --tree  只打印 ANTLR 解析树（扩展 AST 时的调试工具）
    bool showParseTree = false;
    std::string inputFile;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tree") showParseTree = true;
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

    if (showParseTree) {
        std::cout << tree->toStringTree(&parser) << std::endl;
        return 0;
    }

    // ---- 阶段 2：构建 AST（你当前的工作重心）----
    AstBuilder builder;
    Crate* ast = builder.build(tree);

    // 调试用：打印你的 AST，和预期结构对比
    if (ast) {
        ASTPrinter printer(std::cout);
        ast->accept(printer);
    }

    // ---- 之后的阶段（先留位）----
    // 阶段 3：语义检查（作用域、类型检查）—— W4 AST 验收还需要这部分
    // 阶段 4：IR 生成
    // 阶段 5：RISC-V 代码生成
    // 阶段 6：优化

    return 0;
}
