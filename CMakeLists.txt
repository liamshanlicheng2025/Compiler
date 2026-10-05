cmake_minimum_required(VERSION 3.16)
project(RCompiler CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ANTLR4 tool (jar) and C++ runtime install prefix
set(ANTLR4_JAR /opt/antlr4/antlr4-complete.jar CACHE FILEPATH "Path to the ANTLR4 tool jar")
set(ANTLR4_RUNTIME /opt/antlr4/runtime CACHE PATH "ANTLR4 C++ runtime install prefix")

find_package(Java REQUIRED COMPONENTS Runtime)
find_library(ANTLR4_RUNTIME_LIB antlr4-runtime PATHS ${ANTLR4_RUNTIME}/lib REQUIRED)

set(GRAMMAR_DIR ${CMAKE_SOURCE_DIR}/grammar)
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

# Lexer must be generated first: the parser grammar needs RustLexer.tokens.
add_custom_command(
    OUTPUT ${GENERATED_DIR}/RustLexer.cpp ${GENERATED_DIR}/RustLexer.h ${GENERATED_DIR}/RustLexer.tokens
    COMMAND ${Java_JAVA_EXECUTABLE} -jar ${ANTLR4_JAR} -Dlanguage=Cpp -visitor -o ${GENERATED_DIR} RustLexer.g4
    WORKING_DIRECTORY ${GRAMMAR_DIR}
    DEPENDS ${GRAMMAR_DIR}/RustLexer.g4
    COMMENT "Generating RustLexer (ANTLR4)"
)
add_custom_command(
    OUTPUT ${GENERATED_DIR}/RustParser.cpp ${GENERATED_DIR}/RustParser.h
           ${GENERATED_DIR}/RustParserVisitor.cpp ${GENERATED_DIR}/RustParserVisitor.h
           ${GENERATED_DIR}/RustParserBaseVisitor.cpp ${GENERATED_DIR}/RustParserBaseVisitor.h
           ${GENERATED_DIR}/RustParserListener.cpp ${GENERATED_DIR}/RustParserListener.h
           ${GENERATED_DIR}/RustParserBaseListener.cpp ${GENERATED_DIR}/RustParserBaseListener.h
    COMMAND ${Java_JAVA_EXECUTABLE} -jar ${ANTLR4_JAR} -Dlanguage=Cpp -visitor -lib ${GENERATED_DIR} -o ${GENERATED_DIR} RustParser.g4
    WORKING_DIRECTORY ${GRAMMAR_DIR}
    DEPENDS ${GRAMMAR_DIR}/RustParser.g4 ${GENERATED_DIR}/RustLexer.tokens
    COMMENT "Generating RustParser (ANTLR4)"
)

set(GENERATED_SRCS
    ${GENERATED_DIR}/RustLexer.cpp
    ${GENERATED_DIR}/RustParser.cpp
    ${GENERATED_DIR}/RustParserBaseVisitor.cpp
    ${GENERATED_DIR}/RustParserBaseListener.cpp
    ${GENERATED_DIR}/RustParserListener.cpp
    ${GENERATED_DIR}/RustParserVisitor.cpp
)

add_executable(compiler
    src/main.cpp
    src/ast/ast.cpp
    src/ast/ast_printer.cpp
    src/builder/ast_builder.cpp
    grammar/cpp/RustLexerBase.cpp
    grammar/cpp/RustParserBase.cpp
    ${GENERATED_SRCS})
target_include_directories(compiler PRIVATE ${CMAKE_SOURCE_DIR}/src ${GENERATED_DIR} ${CMAKE_SOURCE_DIR}/grammar/cpp ${ANTLR4_RUNTIME}/include/antlr4-runtime)
# The grammars-v4 g4 files declare superClass base classes but don't emit
# the corresponding #include for the C++ target; force-include them.
target_compile_options(compiler PRIVATE "SHELL:-include RustLexerBase.h" "SHELL:-include RustParserBase.h")
target_link_libraries(compiler PRIVATE ${ANTLR4_RUNTIME_LIB} uuid pthread)
