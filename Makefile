# RxOJ 评测用构建：评测机预装 GCC 14 + antlr4-cpp-runtime，无网络、无 Java，
# 所以直接编译提交在 generated/ 里的 ANTLR 预生成代码。
#   make build  → 产出 ./compiler
#   make run    → ./compiler --emit-llvm，stdin 读源码、stdout 出 IR（官方脚本约定）
# 本地开发仍用 CMake（build/），两边不冲突。

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -w
# ANTLR 运行时位置：评测机默认系统路径；本地可用 make ANTLR_RUNTIME=/opt/antlr4/runtime 覆盖
ANTLR_RUNTIME ?= /usr
INCLUDES := -Isrc -Igenerated -Igrammar/cpp -I$(ANTLR_RUNTIME)/include/antlr4-runtime
# g4 声明了 superClass 但不生成对应 #include，强制包含（与 CMake 一致）
FORCE    := -include RustLexerBase.h -include RustParserBase.h
LIBS     := -L$(ANTLR_RUNTIME)/lib -Wl,-rpath,$(ANTLR_RUNTIME)/lib -lantlr4-runtime -luuid -lpthread

SRCS := $(wildcard src/*.cpp src/ast/*.cpp src/builder/*.cpp src/semantic/*.cpp \
                   src/ir/*.cpp grammar/cpp/*.cpp generated/*.cpp)

build:
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(FORCE) $(SRCS) -o compiler $(LIBS)

run:
	@./compiler --emit-llvm

.PHONY: build run
