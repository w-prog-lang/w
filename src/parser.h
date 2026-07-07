#ifndef WLANG_PARSER_H
#define WLANG_PARSER_H

#include "ast.h"
#include "lexer.h"
#include "util.h"

typedef struct {
    Lexer lx;
    Token cur;
    Token prev;
    Arena* arena;
    int had_error;
} Parser;

void parser_init(Parser* p, const char* src, size_t len, Arena* arena);
Node* parser_parse_program(Parser* p);

#endif
