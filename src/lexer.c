#include "lexer.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    const char* word;
    TokenKind kind;
} Keyword;

static const Keyword keywords[] = {
    {"fn", TOK_KW_FN},         {"var", TOK_KW_VAR},       {"return", TOK_KW_RETURN},
    {"if", TOK_KW_IF},         {"else", TOK_KW_ELSE},     {"loop", TOK_KW_LOOP},
    {"struct", TOK_KW_STRUCT}, {"break", TOK_KW_BREAK},   {"continue", TOK_KW_CONTINUE},
};

void lexer_init(Lexer* lx, const char* src, size_t len) {
    lx->src = src;
    lx->pos = 0;
    lx->len = len;
    lx->line = 1;
}

static int at_end(Lexer* lx) { return lx->pos >= lx->len; }

static char peek(Lexer* lx) { return at_end(lx) ? '\0' : lx->src[lx->pos]; }

static char peek_next(Lexer* lx) {
    return (lx->pos + 1 >= lx->len) ? '\0' : lx->src[lx->pos + 1];
}

static char advance(Lexer* lx) {
    char c = lx->src[lx->pos++];
    if (c == '\n') lx->line++;
    return c;
}

static Token make_token(Lexer* lx, TokenKind kind, const char* start, int len) {
    Token t;
    t.kind = kind;
    t.start = start;
    t.len = len;
    t.line = lx->line;
    return t;
}

static void skip_whitespace_and_comments(Lexer* lx) {
    for (;;) {
        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lx);
        } else if (c == '/' && peek_next(lx) == '/') {
            while (!at_end(lx) && peek(lx) != '\n') advance(lx);
        } else {
            return;
        }
    }
}

static TokenKind ident_kind(const char* start, int len) {
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        size_t klen = strlen(keywords[i].word);
        if ((int)klen == len && strncmp(start, keywords[i].word, len) == 0) {
            return keywords[i].kind;
        }
    }
    return TOK_IDENT;
}

static Token lex_plus(Lexer* lx) {
    const char* start = lx->src + lx->pos;
    advance(lx);
    if (peek(lx) == '=') {
        advance(lx);
        return make_token(lx, TOK_PLUS_ASSIGN, start, 2);
    }
    return make_token(lx, TOK_PLUS, start, 1);
}

static Token lex_minus(Lexer* lx) {
    const char* start = lx->src + lx->pos;
    advance(lx);
    if (peek(lx) == '=') {
        advance(lx);
        return make_token(lx, TOK_MINUS_ASSIGN, start, 2);
    }
    return make_token(lx, TOK_MINUS, start, 1);
}

static Token lex_ident(Lexer* lx) {
    const char* start = lx->src + lx->pos;
    while (isalnum((unsigned char)peek(lx)) || peek(lx) == '_') advance(lx);
    int len = (int)((lx->src + lx->pos) - start);
    return make_token(lx, ident_kind(start, len), start, len);
}

static Token lex_number(Lexer* lx) {
    const char* start = lx->src + lx->pos;
    while (isdigit((unsigned char)peek(lx))) advance(lx);
    if (peek(lx) == '.' && isdigit((unsigned char)peek_next(lx))) {
        advance(lx);
        while (isdigit((unsigned char)peek(lx))) advance(lx);
    }
    int len = (int)((lx->src + lx->pos) - start);
    return make_token(lx, TOK_NUM, start, len);
}

static Token lex_string(Lexer* lx) {
    advance(lx);
    const char* start = lx->src + lx->pos;
    while (!at_end(lx) && peek(lx) != '"') {
        // a backslash escapes the next character, so an escaped '"' does not
        // terminate the literal; which escapes are *valid* is checked in sema
        if (peek(lx) == '\\' && lx->pos + 1 < lx->len) {
            advance(lx);
        }
        advance(lx);
    }
    int len = (int)((lx->src + lx->pos) - start);
    if (at_end(lx)) {
        return make_token(lx, TOK_ERROR, start, len);
    }
    advance(lx);
    return make_token(lx, TOK_STRING, start, len);
}

static Token lex_two_char(Lexer* lx, char second, TokenKind two,
                          TokenKind one) {
    const char* start = lx->src + lx->pos;
    advance(lx);
    if (peek(lx) == second) {
        advance(lx);
        return make_token(lx, two, start, 2);
    }
    return make_token(lx, one, start, 1);
}

static Token lex_colon(Lexer* lx) {
    const char* start = lx->src + lx->pos;
    advance(lx);  // consume ':'
    if (peek(lx) == '=') {
        advance(lx);
        return make_token(lx, TOK_DEFINE, start, 2);
    }
    return make_token(lx, TOK_COLON, start, 1);
}

// '#import' <path> -- the whole directive folds into one TOK_IMPORT token
// whose text is the raw path between the angle brackets
static Token lex_import(Lexer* lx) {
    const char* hash = lx->src + lx->pos;
    advance(lx);  // consume '#'

    static const char kw[] = "import";
    for (int i = 0; kw[i]; i++) {
        if (peek(lx) != kw[i]) {
            return make_token(lx, TOK_ERROR, hash,
                              (int)((lx->src + lx->pos) - hash));
        }
        advance(lx);
    }

    while (peek(lx) == ' ' || peek(lx) == '\t') advance(lx);

    if (peek(lx) != '<') {
        return make_token(lx, TOK_ERROR, hash,
                          (int)((lx->src + lx->pos) - hash));
    }
    advance(lx);  // consume '<'

    const char* start = lx->src + lx->pos;
    while (!at_end(lx) && peek(lx) != '>' && peek(lx) != '\n') advance(lx);
    int len = (int)((lx->src + lx->pos) - start);
    if (peek(lx) != '>') {
        return make_token(lx, TOK_ERROR, start, len);
    }
    advance(lx);  // consume '>'
    return make_token(lx, TOK_IMPORT, start, len);
}

static Token lex_lt(Lexer* lx) {
    const char* start = lx->src + lx->pos;
    advance(lx);  // consume '<'
    if (peek(lx) == '=') {
        advance(lx);
        return make_token(lx, TOK_LE, start, 2);
    }
    if (peek(lx) == '-') {
        advance(lx);
        return make_token(lx, TOK_ARROW, start, 2);
    }
    if (peek(lx) == '<') {
        advance(lx);
        return make_token(lx, TOK_SHL, start, 2);
    }
    return make_token(lx, TOK_LT, start, 1);
}

static Token lex_gt(Lexer* lx) {
    const char* start = lx->src + lx->pos;
    advance(lx);  // consume '>'
    if (peek(lx) == '=') {
        advance(lx);
        return make_token(lx, TOK_GE, start, 2);
    }
    if (peek(lx) == '>') {
        advance(lx);
        return make_token(lx, TOK_SHR, start, 2);
    }
    return make_token(lx, TOK_GT, start, 1);
}

Token lexer_next(Lexer* lx) {
    skip_whitespace_and_comments(lx);

    if (at_end(lx)) {
        return make_token(lx, TOK_EOF, lx->src + lx->pos, 0);
    }

    char c = peek(lx);

    if (isalpha((unsigned char)c) || c == '_') return lex_ident(lx);
    if (isdigit((unsigned char)c)) return lex_number(lx);
    if (c == '"') return lex_string(lx);

    const char* start = lx->src + lx->pos;

    switch (c) {
        case '+':
            return lex_plus(lx);
        case '-':
            return lex_minus(lx);
        case '*':
            return lex_two_char(lx, '=', TOK_STAR_ASSIGN, TOK_STAR);
        case '/':
            return lex_two_char(lx, '=', TOK_SLASH_ASSIGN, TOK_SLASH);
        case '%':
            advance(lx);
            return make_token(lx, TOK_PERCENT, start, 1);
        case '(':
            advance(lx);
            return make_token(lx, TOK_LPAREN, start, 1);
        case ')':
            advance(lx);
            return make_token(lx, TOK_RPAREN, start, 1);
        case '{':
            advance(lx);
            return make_token(lx, TOK_LBRACE, start, 1);
        case '}':
            advance(lx);
            return make_token(lx, TOK_RBRACE, start, 1);
        case '[':
            advance(lx);
            return make_token(lx, TOK_LBRACKET, start, 1);
        case ']':
            advance(lx);
            return make_token(lx, TOK_RBRACKET, start, 1);
        case ',':
            advance(lx);
            return make_token(lx, TOK_COMMA, start, 1);
        case ';':
            advance(lx);
            return make_token(lx, TOK_SEMI, start, 1);
        case '.':
            advance(lx);
            return make_token(lx, TOK_DOT, start, 1);
        case ':':
            return lex_colon(lx);
        case '#':
            return lex_import(lx);
        case '<':
            return lex_lt(lx);
        case '=':
            return lex_two_char(lx, '=', TOK_EQ, TOK_ASSIGN);
        case '!':
            return lex_two_char(lx, '=', TOK_NEQ, TOK_BANG);
        case '>':
            return lex_gt(lx);
        case '&':
            return lex_two_char(lx, '&', TOK_AND_AND, TOK_AMP);
        case '|':
            return lex_two_char(lx, '|', TOK_OR_OR, TOK_PIPE);
        case '^':
            advance(lx);
            return make_token(lx, TOK_CARET, start, 1);
        case '~':
            advance(lx);
            return make_token(lx, TOK_TILDE, start, 1);
        default:
            advance(lx);
            return make_token(lx, TOK_ERROR, start, 1);
    }
}

const char* token_kind_name(TokenKind kind) {
    static const char* names[] = {
        "EOF",      "IDENT",       "NUM",          "STRING",   "FN",
        "VAR",      "RETURN",      "IF",           "ELSE",     "LOOP",
        "STRUCT",   "BREAK",       "CONTINUE",     "PLUS",     "MINUS",
        "STAR",     "SLASH",       "PERCENT",      "ASSIGN",   "DEFINE",
        "ARROW",    "PLUS_ASSIGN", "MINUS_ASSIGN", "STAR_ASSIGN", "SLASH_ASSIGN",
        "EQ",       "NEQ",         "LT",           "GT",          "LE",
        "GE",       "BANG",        "AND_AND",      "OR_OR",       "AMP",
        "PIPE",     "CARET",       "TILDE",        "SHL",         "SHR",
        "LPAREN",   "RPAREN",      "LBRACE",       "RBRACE",      "LBRACKET",
        "RBRACKET", "COMMA",       "SEMI",         "COLON",       "DOT",
        "IMPORT",   "ERROR",
    };
    return names[kind];
}
