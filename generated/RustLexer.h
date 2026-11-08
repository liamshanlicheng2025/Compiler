
// Generated from RustLexer.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  RustLexer : public RustLexerBase {
public:
  enum {
    KW_AS = 1, KW_BREAK = 2, KW_CONST = 3, KW_CONTINUE = 4, KW_CRATE = 5, 
    KW_ELSE = 6, KW_ENUM = 7, KW_EXTERN = 8, KW_FALSE = 9, KW_FN = 10, KW_FOR = 11, 
    KW_IF = 12, KW_IMPL = 13, KW_IN = 14, KW_LET = 15, KW_LOOP = 16, KW_MATCH = 17, 
    KW_MOD = 18, KW_MOVE = 19, KW_MUT = 20, KW_PUB = 21, KW_REF = 22, KW_RETURN = 23, 
    KW_SELFVALUE = 24, KW_SELFTYPE = 25, KW_STATIC = 26, KW_STRUCT = 27, 
    KW_SUPER = 28, KW_TRAIT = 29, KW_TRUE = 30, KW_TYPE = 31, KW_UNSAFE = 32, 
    KW_USE = 33, KW_WHERE = 34, KW_WHILE = 35, KW_ASYNC = 36, KW_AWAIT = 37, 
    KW_DYN = 38, KW_ABSTRACT = 39, KW_BECOME = 40, KW_BOX = 41, KW_DO = 42, 
    KW_FINAL = 43, KW_MACRO = 44, KW_OVERRIDE = 45, KW_PRIV = 46, KW_TYPEOF = 47, 
    KW_UNSIZED = 48, KW_VIRTUAL = 49, KW_YIELD = 50, KW_TRY = 51, KW_UNION = 52, 
    KW_STATICLIFETIME = 53, KW_MACRORULES = 54, KW_UNDERLINELIFETIME = 55, 
    KW_DOLLARCRATE = 56, NON_KEYWORD_IDENTIFIER = 57, RAW_IDENTIFIER = 58, 
    LINE_COMMENT = 59, BLOCK_COMMENT = 60, INNER_LINE_DOC = 61, INNER_BLOCK_DOC = 62, 
    OUTER_LINE_DOC = 63, OUTER_BLOCK_DOC = 64, BLOCK_COMMENT_OR_DOC = 65, 
    SHEBANG = 66, WHITESPACE = 67, NEWLINE = 68, CHAR_LITERAL = 69, STRING_LITERAL = 70, 
    RAW_STRING_LITERAL = 71, BYTE_LITERAL = 72, BYTE_STRING_LITERAL = 73, 
    RAW_BYTE_STRING_LITERAL = 74, INTEGER_LITERAL = 75, DEC_LITERAL = 76, 
    HEX_LITERAL = 77, OCT_LITERAL = 78, BIN_LITERAL = 79, FLOAT_LITERAL = 80, 
    LIFETIME_OR_LABEL = 81, PLUS = 82, MINUS = 83, STAR = 84, SLASH = 85, 
    PERCENT = 86, CARET = 87, NOT = 88, AND = 89, OR = 90, ANDAND = 91, 
    OROR = 92, PLUSEQ = 93, MINUSEQ = 94, STAREQ = 95, SLASHEQ = 96, PERCENTEQ = 97, 
    CARETEQ = 98, ANDEQ = 99, OREQ = 100, SHLEQ = 101, SHREQ = 102, EQ = 103, 
    EQEQ = 104, NE = 105, GT = 106, LT = 107, GE = 108, LE = 109, AT = 110, 
    UNDERSCORE = 111, DOT = 112, DOTDOT = 113, DOTDOTDOT = 114, DOTDOTEQ = 115, 
    COMMA = 116, SEMI = 117, COLON = 118, PATHSEP = 119, RARROW = 120, FATARROW = 121, 
    POUND = 122, DOLLAR = 123, QUESTION = 124, LCURLYBRACE = 125, RCURLYBRACE = 126, 
    LSQUAREBRACKET = 127, RSQUAREBRACKET = 128, LPAREN = 129, RPAREN = 130
  };

  explicit RustLexer(antlr4::CharStream *input);

  ~RustLexer() override;


  std::string getGrammarFileName() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const std::vector<std::string>& getChannelNames() const override;

  const std::vector<std::string>& getModeNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  const antlr4::atn::ATN& getATN() const override;

  bool sempred(antlr4::RuleContext *_localctx, size_t ruleIndex, size_t predicateIndex) override;

  // By default the static state used to implement the lexer is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:

  // Individual action functions triggered by action() above.

  // Individual semantic predicate functions triggered by sempred() above.
  bool SHEBANGSempred(antlr4::RuleContext *_localctx, size_t predicateIndex);
  bool FLOAT_LITERALSempred(antlr4::RuleContext *_localctx, size_t predicateIndex);

};

