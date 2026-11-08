# Rx Compiler 课程统一接口（模板 config.mk 约定，评测机与本机 test runner 共用）
# 占位符：{source} 源文件 {output} 输出 {runtime} 共享运行时汇编 {stdout} {profile}

# 构建编译器（g++ 直编，generated/ 里是预生成的 ANTLR 代码，无需 Java）；
# runtime.s 是预编译的运行时（内建 IO 函数 → REIMU libc），链进每个测试程序
BUILD = make build && cp runtime.s {runtime}

# 语义检查：exit 0 接受 / exit 1 拒绝
SEMANTIC = ./compiler {source}

# IR 阶段：输出 LLVM IR 文本到 {output}
IR = ./compiler --emit-llvm {source} -o {output}

# Codegen 阶段（过渡方案：IR 经 clang 落汇编；自写后端就绪后替换为
#   ./compiler --emit-asm {source} -o {output}）
CODEGEN = ./compiler --emit-llvm {source} -o {output}.ll && \
    clang -S -x ir --target=riscv32-unknown-none-elf -march=rv32im -mabi=ilp32 \
    -O0 -mllvm -riscv-no-aliases {output}.ll -o {output} && \
    sed -i 's/@plt//g' {output}

# 本地用 ravel 跑；评测机的 RUN 由评测环境提供（REIMU）
RUN = $(HOME)/tools/ravel/install/bin/ravel --input-file=/dev/null \
    --output-file={stdout} {output} {runtime}
