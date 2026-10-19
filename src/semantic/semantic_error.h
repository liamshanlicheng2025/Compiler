#pragma once

// 语义错误：任何一个检查失败就抛出，main 里捕获后以非 0 退出
#include <stdexcept>
#include <string>

struct SemanticError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// 统一报错入口：[[noreturn]] 让编译器知道调用后不会返回，
// 写 "if (bad) semaError(...);" 后不需要补 return
[[noreturn]] inline void semaError(const std::string& msg) {
    throw SemanticError(msg);
}
