#ifndef WLANG_AST_H
#define WLANG_AST_H

#include "lexer.h"
#include "util.h"

typedef struct {
    const char* name;
    int len;
} TypeRef;

typedef enum {
    NODE_PROGRAM,
    NODE_FUNC_DECL,
    NODE_BLOCK,
    NODE_VAR_DECL,
    NODE_ASSIGN,
    NODE_IF,
    NODE_LOOP,
    NODE_RETURN,
    NODE_BINOP,
    NODE_UNARY,
    NODE_CALL,
    NODE_IDENT,
    NODE_NUM,
    NODE_BREAK,
    NODE_CONTINUE,
} NodeKind;

typedef struct Node Node;

typedef struct {
    const char* name;
    int name_len;
    TypeRef type;
} Param;

struct Node {
    NodeKind kind;
    int line;

    union {
        struct {
            PtrList funcs;
        } program;

        struct {
            const char* name;
            int name_len;
            TypeRef return_type;
            Param* params;
            int param_count;
            Node* body;
        } func_decl;

        struct {
            PtrList stmts;
        } block;

        struct {
            const char* name;
            int name_len;
            TypeRef type;
            Node* init;
            int is_const;
        } var_decl;

        struct {
            const char* name;
            int name_len;
            Node* value;
        } assign;

        struct {
            Node* cond;
            Node* then_block;
            Node* else_block;
        } if_stmt;

        struct {
            Node* expr;
        } return_stmt;

        struct {
            TokenKind op;
            Node* left;
            Node* right;
        } binop;

        struct {
            TokenKind op;  // TOK_BANG or TOK_MINUS
            Node* operand;
        } unary;

        struct {
            const char* name;
            int name_len;
            Node** args;
            int arg_count;
        } call;

        struct {
            const char* name;
            int len;
        } ident;

        struct {
            const char* text;
            int len;
        } num;

        struct {
            Node* init;  // NULL if no init clause
            Node* cond;  // NULL if infinite loop (no cond at all)
            Node* step;  // NULL if no step clause
            Node* body;
        } loop;
    } as;
};

Node* ast_new(Arena* a, NodeKind kind, int line);
const char* node_kind_name(NodeKind kind);

#endif
