#include <fstream>
#include <iostream>

#include "antlr4-runtime.h"
#include "RustLexer.h"
#include "RustParser.h"

#include "builder/ast_builder.h"
#include "ast/ast_printer.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <file.rs>" << std::endl;
        return 1;
    }
    std::ifstream stream(argv[1]);
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
