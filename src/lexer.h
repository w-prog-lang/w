#ifndef WLANG_LEXER_H
#define WLANG_LEXER_H

#include <stddef.h>

typedef enum {
    TOK_EOF,
    TOK_IDENT,
    TOK_NUM,
    TOK_STRING,

    TOK_KW_FN,
    TOK_KW_LET,
    TOK_KW_VAR,
    TOK_KW_RETURN,
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_WHILE,
    TOK_KW_STRUCT,

    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_ASSIGN,  // =
    TOK_DEFINE,  // :=
    TOK_ARROW,   // <-
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_GT,
    TOK_LE,
    TOK_GE,

    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_SEMI,
    TOK_COLON,
    TOK_DOT,

    TOK_ERROR,
} TokenKind;

typedef struct {
    TokenKind kind;
    const char* start;
    int len;
    int line;
} Token;

typedef struct {
    const char* src;
    size_t pos;
    size_t len;
    int line;
} Lexer;

void lexer_init(Lexer* lx, const char* src, size_t len);
Token lexer_next(Lexer* lx);
const char* token_kind_name(TokenKind kind);

#endif
