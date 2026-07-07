#include "ast.h"

Node* ast_new(Arena* a, NodeKind kind, int line) {
    Node* n = arena_alloc(a, sizeof(Node));
    n->kind = kind;
    n->line = line;
    return n;
}

const char* node_kind_name(NodeKind kind) {
    static const char* names[] = {
        "PROGRAM", "FUNC_DECL", "BLOCK", "VAR_DECL", "ASSIGN",
        "IF",      "LOOP",      "RETURN", "BINOP",   "UNARY",
        "CALL",    "IDENT",     "NUM",    "BREAK",   "CONTINUE",
    };
    return names[kind];
}
