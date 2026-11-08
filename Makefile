# RxOJ 评测用构建：评测机只有 GCC，无网络、无 Java、ANTLR 头文件不一定装，
# 所以 ANTLR 运行时（4.13.2 头文件 + 静态库）vendored 在 vendor/ 里，
# 预生成解析器代码在 generated/——整个构建对评测机环境零依赖。
#   make build  → 产出 ./compiler
#   make run    → ./compiler --emit-llvm，stdin 读源码、stdout 出 IR（官方脚本约定）
# 本地开发仍用 CMake（build/），两边不冲突。

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -w
# ANTLR 运行时前缀：默认 vendored 静态库；本地也可用 ANTLR_PREFIX=/opt/antlr4/runtime 覆盖
ANTLR_PREFIX ?= vendor/antlr4-runtime
INCLUDES := -Isrc -Igenerated -Igrammar/cpp -I$(ANTLR_PREFIX)/include/antlr4-runtime
# g4 声明了 superClass 但不生成对应 #include，强制包含（与 CMake 一致）
FORCE    := -include RustLexerBase.h -include RustParserBase.h
# 静态链接 vendored 运行时；-l:libuuid.so.1 直接按 soname 链（不依赖 uuid-dev 的 .so 符号链接）
LIBS     := $(ANTLR_PREFIX)/lib/libantlr4-runtime.a -l:libuuid.so.1 -lpthread

SRCS := $(wildcard src/*.cpp src/ast/*.cpp src/builder/*.cpp src/semantic/*.cpp \
                   src/ir/*.cpp grammar/cpp/*.cpp generated/*.cpp)

build:
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(FORCE) $(SRCS) -o compiler $(LIBS)

run:
	@./compiler --emit-llvm

.PHONY: build run
