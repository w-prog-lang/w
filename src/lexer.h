#ifndef WLANG_LEXER_H
#define WLANG_LEXER_H

#include <stddef.h>

typedef enum {
    TOK_EOF,
    TOK_IDENT,
    TOK_NUM,
    TOK_STRING,

    TOK_KW_FN,
    TOK_KW_VAR,
    TOK_KW_RETURN,
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_LOOP,
    TOK_KW_STRUCT,
    TOK_KW_BREAK,
    TOK_KW_CONTINUE,
    TOK_KW_TRUE,
    TOK_KW_FALSE,

    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,       // %
    TOK_ASSIGN,        // =
    TOK_DEFINE,        // :=
    TOK_ARROW,         // <-
    TOK_PLUS_ASSIGN,   // +=
    TOK_MINUS_ASSIGN,  // -=
    TOK_STAR_ASSIGN,   // *=
    TOK_SLASH_ASSIGN,  // /=
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_GT,
    TOK_LE,
    TOK_GE,
    TOK_BANG,      // !
    TOK_AND_AND,   // &&
    TOK_OR_OR,     // ||
    TOK_AMP,       // &
    TOK_PIPE,      // |
    TOK_CARET,     // ^
    TOK_TILDE,     // ~
    TOK_SHL,       // <<
    TOK_SHR,       // >>

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

    TOK_IMPORT,  // '#import <path>' -- token text is the path between <>

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
